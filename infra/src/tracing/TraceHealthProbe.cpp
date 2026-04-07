#include "tracing/TraceHealthProbe.h"

#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::tracing {
namespace {

constexpr std::string_view kTraceHealthProbeSourceRef = "TraceHealthProbe";

[[nodiscard]] TraceOperationStatus make_trace_failure(TraceErrorCode code,
                                                      std::string message,
                                                      std::string stage) {
  const auto mapping = map_trace_error_code(code);
  return TraceOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kTraceHealthProbeSourceRef) + ":" +
          std::string(trace_error_code_name(code)));
}

}  // namespace

TraceHealthProbe::TraceHealthProbe(std::uint32_t consecutive_failure_threshold)
    : consecutive_failure_threshold_(consecutive_failure_threshold > 0U
                                         ? consecutive_failure_threshold
                                         : 2U) {}

TraceOperationStatus TraceHealthProbe::observe_result(
    const TraceOperationStatus& pipeline_status,
    const TraceModuleSnapshot& module_snapshot) {
  if (!module_snapshot.is_valid()) {
    return invalid_request(
        "trace health probe requires a valid module snapshot before evaluating recovery state",
        "tracing.health.observe_result");
  }

  if (pipeline_status.ok && !module_snapshot.degraded) {
    if (snapshot_.degraded_mode) {
      return recover_to_healthy(
          "tracing exporter recovered and health state returned to healthy",
          module_snapshot);
    }

    snapshot_.module_snapshot = module_snapshot;
    snapshot_.degraded_mode = false;
    snapshot_.consecutive_failure_total = 0U;
    snapshot_.last_error_code.reset();
    snapshot_.last_failure_reason.clear();
    snapshot_.detail_ref = make_detail_ref(false, std::nullopt, "healthy");
    return TraceOperationStatus::success("trace-health://healthy-observed");
  }

  ++snapshot_.consecutive_failure_total;
  const auto error_code = infer_error_code(pipeline_status).value_or(
      module_snapshot.degraded ? TraceErrorCode::ExportFailure
                               : TraceErrorCode::ConfigInvalid);
  const std::string failure_reason =
      pipeline_status.error.has_value()
          ? pipeline_status.error->details.message
          : std::string(
                "tracing pipeline reported a non-healthy result and health monitoring remains active");

  if (snapshot_.degraded_mode ||
      snapshot_.consecutive_failure_total >= consecutive_failure_threshold_) {
    return enter_degraded(error_code, failure_reason, module_snapshot);
  }

  snapshot_.module_snapshot = module_snapshot;
  snapshot_.last_error_code = error_code;
  snapshot_.last_failure_reason = failure_reason;
  snapshot_.detail_ref = make_detail_ref(false, error_code, "failure_observed");
  return TraceOperationStatus::success("trace-health://failure-observed");
}

TraceOperationStatus TraceHealthProbe::enter_degraded(
    TraceErrorCode error_code,
    std::string reason,
    TraceModuleSnapshot module_snapshot) {
  if (reason.empty() || !module_snapshot.is_valid()) {
    return invalid_request(
        "trace health probe requires both a failure reason and a valid snapshot before entering degraded mode",
        "tracing.health.enter_degraded");
  }

  const bool state_changed = !snapshot_.degraded_mode;
  snapshot_.module_snapshot = std::move(module_snapshot);
  snapshot_.degraded_mode = true;
  if (state_changed) {
    ++snapshot_.degrade_enter_total;
  }
  snapshot_.last_error_code = error_code;
  snapshot_.last_failure_reason = std::move(reason);
  snapshot_.detail_ref = make_detail_ref(true, error_code, "state");

  return TraceOperationStatus::success(state_changed
                                           ? "trace-health://degraded-entered"
                                           : "trace-health://degraded-retained");
}

TraceOperationStatus TraceHealthProbe::recover_to_healthy(
    std::string reason,
    TraceModuleSnapshot module_snapshot) {
  if (reason.empty() || !module_snapshot.is_valid()) {
    return invalid_request(
        "trace health probe requires both a recovery reason and a valid snapshot before returning to healthy mode",
        "tracing.health.recover_to_healthy");
  }

  const bool state_changed = snapshot_.degraded_mode;
  snapshot_.module_snapshot = std::move(module_snapshot);
  snapshot_.degraded_mode = false;
  snapshot_.consecutive_failure_total = 0U;
  if (state_changed) {
    ++snapshot_.recovery_success_total;
  }
  snapshot_.last_error_code.reset();
  snapshot_.last_failure_reason.clear();
  snapshot_.detail_ref = make_detail_ref(false, std::nullopt, "healthy");

  return TraceOperationStatus::success(state_changed ? "trace-health://recovered"
                                                     : "trace-health://healthy");
}

bool TraceHealthProbe::is_degraded() const {
  return snapshot_.degraded_mode;
}

std::uint64_t TraceHealthProbe::consecutive_failure_total() const {
  return snapshot_.consecutive_failure_total;
}

std::uint64_t TraceHealthProbe::degrade_enter_total() const {
  return snapshot_.degrade_enter_total;
}

std::uint64_t TraceHealthProbe::recovery_success_total() const {
  return snapshot_.recovery_success_total;
}

const TraceHealthSnapshot& TraceHealthProbe::snapshot() const {
  return snapshot_;
}

const std::string& TraceHealthProbe::last_failure_reason() const {
  return snapshot_.last_failure_reason;
}

std::optional<TraceErrorCode> TraceHealthProbe::last_error_code() const {
  return snapshot_.last_error_code;
}

TraceOperationStatus TraceHealthProbe::invalid_request(std::string message,
                                                       std::string stage) const {
  return make_trace_failure(TraceErrorCode::ConfigInvalid,
                            std::move(message),
                            std::move(stage));
}

std::optional<TraceErrorCode> TraceHealthProbe::infer_error_code(
    const TraceOperationStatus& pipeline_status) {
  if (!pipeline_status.error.has_value()) {
    return std::nullopt;
  }

  const std::string& source_ref = pipeline_status.error->source_ref.ref_id;
  for (const auto code : {TraceErrorCode::ProviderNotReady,
                          TraceErrorCode::InvalidContext,
                          TraceErrorCode::QueueFull,
                          TraceErrorCode::ExportTimeout,
                          TraceErrorCode::ExportFailure,
                          TraceErrorCode::ShutdownTimeout,
                          TraceErrorCode::ConfigInvalid}) {
    if (source_ref.find(trace_error_code_name(code)) != std::string::npos) {
      return code;
    }
  }

  return std::nullopt;
}

std::string TraceHealthProbe::make_detail_ref(bool degraded_mode,
                                              std::optional<TraceErrorCode> error_code,
                                              std::string_view fallback_segment) {
  std::string detail_ref = std::string(kTraceHealthDetailNamespace) + "/";
  if (degraded_mode) {
    detail_ref += "degraded/";
  } else if (error_code.has_value()) {
    detail_ref += "observing/";
  }

  if (error_code.has_value()) {
    detail_ref += trace_error_code_name(*error_code);
  } else {
    detail_ref += fallback_segment;
  }

  return detail_ref;
}

}  // namespace dasall::infra::tracing
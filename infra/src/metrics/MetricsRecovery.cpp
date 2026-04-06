#include "metrics/MetricsRecovery.h"

#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricsRecoverySourceRef = "MetricsRecovery";

[[nodiscard]] MetricsOperationStatus make_recovery_failure(MetricsErrorCode code,
                                                           std::string message,
                                                           std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kMetricsRecoverySourceRef) + ":" +
          std::string(metrics_error_code_name(code)));
}

}  // namespace

MetricsRecovery::MetricsRecovery(
    std::uint32_t consecutive_failure_threshold,
    std::shared_ptr<IMetricsRecoveryLogHook> log_hook)
    : consecutive_failure_threshold_(consecutive_failure_threshold > 0U
                                         ? consecutive_failure_threshold
                                         : 2U),
      log_hook_(std::move(log_hook)) {
  module_snapshot_.exporter_state = "uninitialized";
}

MetricsOperationStatus MetricsRecovery::observe_export_result(
    const MetricsOperationStatus& export_status,
    const MetricsModuleSnapshot& exporter_snapshot) {
  if (!exporter_snapshot.is_valid()) {
    return invalid_request(
        "metrics recovery requires a valid exporter snapshot before evaluating recovery state",
        "metrics.recovery.observe_export_result");
  }

  if (export_status.ok && !exporter_snapshot.degraded) {
    if (degraded_) {
      return recover_to_healthy(
          "metrics exporter recovered and the recovery state returned to healthy",
          exporter_snapshot);
    }

    consecutive_failure_total_ = 0;
    module_snapshot_ = exporter_snapshot;
    module_snapshot_.degraded = false;
    last_failure_reason_.clear();
    last_error_code_.reset();
    return MetricsOperationStatus::success("metrics-recovery://healthy-observed");
  }

  ++consecutive_failure_total_;
  module_snapshot_ = exporter_snapshot;
  module_snapshot_.degraded = degraded_;
  last_error_code_ =
      infer_error_code(export_status).value_or(MetricsErrorCode::ExportFailure);
  last_failure_reason_ =
      export_status.error.has_value()
          ? export_status.error->details.message
          : std::string(
                "metrics exporter reported a failure and recovery monitoring remains active");

  if (degraded_ || consecutive_failure_total_ >= consecutive_failure_threshold_) {
    return enter_degraded(*last_error_code_, last_failure_reason_, exporter_snapshot);
  }

  return MetricsOperationStatus::success("metrics-recovery://failure-observed");
}

MetricsOperationStatus MetricsRecovery::enter_degraded(MetricsErrorCode error_code,
                                                       std::string reason,
                                                       MetricsModuleSnapshot snapshot) {
  if (reason.empty() || !snapshot.is_valid()) {
    return invalid_request(
        "metrics recovery requires both a failure reason and a valid snapshot before entering degraded mode",
        "metrics.recovery.enter_degraded");
  }

  const bool state_changed = !degraded_;
  degraded_ = true;
  module_snapshot_ = std::move(snapshot);
  module_snapshot_.degraded = true;
  last_failure_reason_ = reason;
  last_error_code_ = error_code;
  if (state_changed) {
    ++degrade_enter_total_;
  }

  prepare_event(state_changed ? "enter_degraded" : "degraded_still_active",
                std::move(reason),
                error_code,
                module_snapshot_);

  const auto emit_status = emit_recovery_event("metrics.recovery.enter_degraded");
  if (!emit_status.ok) {
    return emit_status;
  }

  return MetricsOperationStatus::success(state_changed
                                             ? "metrics-recovery://degraded-entered"
                                             : "metrics-recovery://degraded-retained");
}

MetricsOperationStatus MetricsRecovery::recover_to_healthy(std::string reason,
                                                           MetricsModuleSnapshot snapshot) {
  if (reason.empty() || !snapshot.is_valid()) {
    return invalid_request(
        "metrics recovery requires both a recovery reason and a valid snapshot before returning to healthy mode",
        "metrics.recovery.recover_to_healthy");
  }

  const bool state_changed = degraded_;
  degraded_ = false;
  consecutive_failure_total_ = 0;
  module_snapshot_ = std::move(snapshot);
  module_snapshot_.degraded = false;
  last_failure_reason_ = reason;
  last_error_code_.reset();
  if (state_changed) {
    ++recovery_success_total_;
  }

  prepare_event(state_changed ? "recover_to_healthy" : "healthy_still_active",
                std::move(reason),
                std::nullopt,
                module_snapshot_);

  const auto emit_status = emit_recovery_event("metrics.recovery.recover_to_healthy");
  if (!emit_status.ok) {
    return emit_status;
  }

  return MetricsOperationStatus::success(state_changed
                                             ? "metrics-recovery://recovered"
                                             : "metrics-recovery://healthy");
}

MetricsOperationStatus MetricsRecovery::emit_recovery_event(std::string stage) {
  if (!last_event_.has_value()) {
    return invalid_request(
        "metrics recovery cannot emit a recovery event before a state transition is recorded",
        "metrics.recovery.emit_recovery_event");
  }

  last_event_->stage = std::move(stage);
  if (!last_event_->is_valid()) {
    return invalid_request(
        "metrics recovery event became invalid before the log hook dispatch step",
        "metrics.recovery.emit_recovery_event");
  }

  if (!log_hook_) {
    return MetricsOperationStatus::success("metrics-recovery://event-buffered");
  }

  return log_hook_->write_recovery_event(*last_event_);
}

bool MetricsRecovery::is_degraded() const {
  return degraded_;
}

std::uint64_t MetricsRecovery::consecutive_failure_total() const {
  return consecutive_failure_total_;
}

std::uint64_t MetricsRecovery::degrade_enter_total() const {
  return degrade_enter_total_;
}

std::uint64_t MetricsRecovery::recovery_success_total() const {
  return recovery_success_total_;
}

const std::optional<MetricsRecoveryEvent>& MetricsRecovery::last_event() const {
  return last_event_;
}

const MetricsModuleSnapshot& MetricsRecovery::module_snapshot() const {
  return module_snapshot_;
}

const std::string& MetricsRecovery::last_failure_reason() const {
  return last_failure_reason_;
}

std::optional<MetricsErrorCode> MetricsRecovery::last_error_code() const {
  return last_error_code_;
}

MetricsOperationStatus MetricsRecovery::invalid_request(std::string message,
                                                        std::string stage) const {
  return make_recovery_failure(MetricsErrorCode::ConfigInvalid,
                               std::move(message),
                               std::move(stage));
}

std::optional<MetricsErrorCode> MetricsRecovery::infer_error_code(
    const MetricsOperationStatus& export_status) {
  if (export_status.error.has_value()) {
    const std::string& source_ref = export_status.error->source_ref.ref_id;
    if (source_ref.find(metrics_error_code_name(MetricsErrorCode::ExportTimeout)) !=
        std::string::npos) {
      return MetricsErrorCode::ExportTimeout;
    }

    if (source_ref.find(metrics_error_code_name(MetricsErrorCode::ExportFailure)) !=
        std::string::npos) {
      return MetricsErrorCode::ExportFailure;
    }

    if (source_ref.find(metrics_error_code_name(MetricsErrorCode::QueueFull)) !=
        std::string::npos) {
      return MetricsErrorCode::QueueFull;
    }
  }

  return std::nullopt;
}

void MetricsRecovery::prepare_event(
    std::string action,
    std::string reason,
    std::optional<MetricsErrorCode> error_code,
    const MetricsModuleSnapshot& snapshot) {
  last_event_ = MetricsRecoveryEvent{
      .action = std::move(action),
      .stage = {},
      .reason = std::move(reason),
      .error_code = error_code,
      .module_snapshot = snapshot,
      .consecutive_failure_total = consecutive_failure_total_,
      .degrade_enter_total = degrade_enter_total_,
      .recovery_success_total = recovery_success_total_,
  };
}

}  // namespace dasall::infra::metrics
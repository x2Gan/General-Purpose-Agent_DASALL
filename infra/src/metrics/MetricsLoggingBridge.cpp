#include "metrics/MetricsLoggingBridge.h"

#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricsLoggingBridgeSourceRef =
    "MetricsLoggingBridge";
constexpr std::string_view kMetricsLoggingBridgeStage =
    "metrics.logging.bridge";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value,
                                        std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] logging::LogLevel log_level_from_outcome(
    MetricsBridgeEventOutcome outcome) {
  switch (outcome) {
    case MetricsBridgeEventOutcome::Success:
      return logging::LogLevel::Info;
    case MetricsBridgeEventOutcome::Failure:
      return logging::LogLevel::Error;
    case MetricsBridgeEventOutcome::Degraded:
      return logging::LogLevel::Warn;
  }

  return logging::LogLevel::Warn;
}

[[nodiscard]] MetricsBridgeEvent make_recovery_bridge_event(
    const MetricsRecoveryEvent& recovery_event) {
  const MetricsBridgeEventOutcome outcome =
      recovery_event.error_code.has_value()
          ? MetricsBridgeEventOutcome::Degraded
          : MetricsBridgeEventOutcome::Success;

  return MetricsBridgeEvent{
      .kind = MetricsBridgeEventKind::RecoveryTransition,
      .action = recovery_event.action,
      .stage = recovery_event.stage.empty()
                   ? std::string("metrics.recovery.unknown")
                   : recovery_event.stage,
      .outcome = outcome,
      .reason = recovery_event.reason,
      .error_code = recovery_event.error_code,
      .module_snapshot = recovery_event.module_snapshot,
      .context = MetricsBridgeContext{},
      .detail_ref = std::string("metrics://recovery/") + recovery_event.action,
      .config_version = {},
      .previous_config_version = {},
      .consecutive_failure_total = recovery_event.consecutive_failure_total,
      .degrade_enter_total = recovery_event.degrade_enter_total,
      .recovery_success_total = recovery_event.recovery_success_total,
      .timestamp_ms = current_time_unix_ms(),
  };
}

}  // namespace

MetricsLoggingBridge::MetricsLoggingBridge(std::shared_ptr<logging::ILogger> logger,
                                           MetricsLoggingBridgeOptions options)
    : logger_(std::move(logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "metrics://logging/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "metrics://logging/");
}

void MetricsLoggingBridge::set_logger(std::shared_ptr<logging::ILogger> logger) {
  logger_ = std::move(logger);
}

MetricsLoggingWriteResult MetricsLoggingBridge::write_log_event(
    const MetricsBridgeEvent& event) {
  const std::string detail_ref = event.detail_ref.empty()
                                     ? options_.detail_ref_prefix + "invalid_event"
                                     : event.detail_ref;
  if (!event.is_valid()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return MetricsLoggingWriteResult{
        .emitted = false,
        .log_event = {},
        .write_result = logging::LogWriteResult::failure(
            contracts::ResultCode::ValidationFieldMissing,
            "metrics logging bridge requires a valid MetricsBridgeEvent before dispatch",
            event.stage.empty() ? std::string(kMetricsLoggingBridgeStage)
                                : event.stage,
            std::string(kMetricsLoggingBridgeSourceRef)),
    };
  }

  if (!logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, event.detail_ref);
    return MetricsLoggingWriteResult{
        .emitted = false,
        .log_event = {},
        .write_result = logging::LogWriteResult::failure(
            contracts::ResultCode::RuntimeRetryExhausted,
            "metrics logging bridge requires an ILogger sink before dispatch",
            event.stage,
            std::string(kMetricsLoggingBridgeSourceRef)),
    };
  }

  auto log_event = make_log_event(event);
  if (!log_event.attrs_are_serializable() || !log_event.has_timestamp()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, event.detail_ref);
    return MetricsLoggingWriteResult{
        .emitted = false,
        .log_event = std::move(log_event),
        .write_result = logging::LogWriteResult::failure(
            contracts::ResultCode::ValidationFieldMissing,
            "metrics logging bridge produced a non-serializable log payload",
            event.stage,
            std::string(kMetricsLoggingBridgeSourceRef)),
    };
  }

  const auto write_result = logger_->log(log_event);
  if (!write_result.ok) {
    record_failure(write_result.result_code, event.detail_ref);
    return MetricsLoggingWriteResult{
        .emitted = false,
        .log_event = log_event,
        .write_result = write_result,
    };
  }

  ++emitted_total_;
  record_success(event.detail_ref);
  return MetricsLoggingWriteResult{
      .emitted = true,
      .log_event = log_event,
      .write_result = write_result,
  };
}

MetricsOperationStatus MetricsLoggingBridge::write_recovery_event(
    const MetricsRecoveryEvent& event) {
  (void)write_log_event(make_recovery_bridge_event(event));
  return MetricsOperationStatus::success("metrics-logging-bridge://best-effort");
}

MetricsLoggingBridgeStatus MetricsLoggingBridge::get_status() const {
  return MetricsLoggingBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

logging::LogEvent MetricsLoggingBridge::make_log_event(
    const MetricsBridgeEvent& event) const {
  logging::LogEvent::AttributeMap attrs;
  attrs.emplace("bridge_kind",
                std::string(metrics_bridge_event_kind_name(event.kind)));
  attrs.emplace("action", event.action);
  attrs.emplace("stage", event.stage);
  attrs.emplace("outcome",
                std::string(metrics_bridge_event_outcome_name(event.outcome)));
  attrs.emplace("detail_ref", event.detail_ref);
  attrs.emplace("worker_type", event.context.worker_type);
  attrs.emplace("request_id", event.context.infra_context.request_id);
  attrs.emplace("session_id", event.context.infra_context.session_id);
  attrs.emplace("trace_id", event.context.infra_context.trace_id);
  attrs.emplace("task_id", event.context.infra_context.task_id);
  attrs.emplace("parent_task_id", event.context.infra_context.parent_task_id);
  attrs.emplace("lease_id", event.context.infra_context.lease_id);
  attrs.emplace("exporter_state", event.module_snapshot.exporter_state);
  attrs.emplace("queue_depth", std::to_string(event.module_snapshot.queue_depth));
  attrs.emplace("guard_reject_total",
                std::to_string(event.module_snapshot.guard_reject_total));
  attrs.emplace("degraded",
                event.module_snapshot.degraded ? "true" : "false");
  attrs.emplace("consecutive_failure_total",
                std::to_string(event.consecutive_failure_total));
  attrs.emplace("degrade_enter_total",
                std::to_string(event.degrade_enter_total));
  attrs.emplace("recovery_success_total",
                std::to_string(event.recovery_success_total));
  attrs.emplace("error_code",
                event.error_code.has_value()
                    ? std::string(metrics_error_code_name(*event.error_code))
                    : std::string("none"));

  if (!event.config_version.empty()) {
    attrs.emplace("config_version", event.config_version);
  }

  if (!event.previous_config_version.empty()) {
    attrs.emplace("previous_config_version", event.previous_config_version);
  }

  return logging::LogEvent{
      .level = log_level_from_outcome(event.outcome),
      .module = std::string("metrics"),
      .message = std::string("metrics ") +
                 std::string(metrics_bridge_event_kind_name(event.kind)) + " " +
                 event.action + ": " + event.reason,
      .attrs = std::move(attrs),
      .ts = event.timestamp_ms,
  };
}

void MetricsLoggingBridge::record_success(const std::string& detail_ref) {
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void MetricsLoggingBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::infra::metrics
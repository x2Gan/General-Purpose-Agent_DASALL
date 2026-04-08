#include "watchdog/WatchdogAuditBridge.h"

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

#include "watchdog/WatchdogErrors.h"

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kWatchdogAuditBridgeSourceRef =
    "WatchdogAuditBridge";
constexpr std::string_view kWatchdogAuditWorkerType = "infra.watchdog";
constexpr std::string_view kWatchdogAuditActor = "system/watchdog";

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

[[nodiscard]] AuditWriteOutcome make_write_failure_outcome(
    contracts::ResultCode result_code) {
  return AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = result_code,
  };
}

[[nodiscard]] std::string describe_write_failure(
    const AuditWriteOutcome& write_outcome) {
  if (!write_outcome.has_consistent_state()) {
    return "watchdog audit sink returned an inconsistent write outcome";
  }

  if (write_outcome.error_code.has_value()) {
    return "watchdog audit sink returned a failing write outcome";
  }

  return "watchdog audit sink did not report success or degraded success";
}

[[nodiscard]] std::string result_code_name(contracts::ResultCode result_code) {
  switch (result_code) {
    case contracts::ResultCode::ValidationFieldMissing:
      return "ValidationFieldMissing";
    case contracts::ResultCode::PolicyDenied:
      return "PolicyDenied";
    case contracts::ResultCode::ToolExecutionFailed:
      return "ToolExecutionFailed";
    case contracts::ResultCode::ProviderTimeout:
      return "ProviderTimeout";
    case contracts::ResultCode::RuntimeRetryExhausted:
      return "RuntimeRetryExhausted";
  }

  return "Unknown";
}

[[nodiscard]] AuditOutcome map_audit_outcome(
    WatchdogTimeoutLevel timeout_level) {
  switch (timeout_level) {
    case WatchdogTimeoutLevel::Critical:
      return AuditOutcome::Failed;
    case WatchdogTimeoutLevel::Fatal:
      return AuditOutcome::Escalated;
    case WatchdogTimeoutLevel::Warning:
      return AuditOutcome::Succeeded;
    case WatchdogTimeoutLevel::Unspecified:
      break;
  }

  return AuditOutcome::Unspecified;
}

[[nodiscard]] std::string make_side_effect(std::string_view key,
                                           std::string value) {
  return std::string(key) + ":" + std::move(value);
}

[[nodiscard]] std::string detail_suffix_for_level(
    WatchdogTimeoutLevel timeout_level) {
  return std::string(watchdog_timeout_level_name(timeout_level));
}

}  // namespace

WatchdogAuditBridge::WatchdogAuditBridge(
    std::shared_ptr<audit::IAuditLogger> audit_logger,
    WatchdogAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://watchdog/audit/") +
                       "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://watchdog/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "watchdog-audit-event-");
}

void WatchdogAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

WatchdogAuditWriteResult WatchdogAuditBridge::write_timeout_audit(
    const TimeoutDecision& decision) {
  const auto detail_suffix = detail_suffix_for_level(decision.timeout_level);
  const auto audit_write_fail_mapping =
      map_watchdog_error_code(WatchdogErrorCode::AuditWriteFail);

  if (!decision.has_required_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   detail_suffix + "/invalid_decision");
    return WatchdogAuditWriteResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog audit bridge requires a valid TimeoutDecision before emit",
        "watchdog.audit",
        std::string(kWatchdogAuditBridgeSourceRef));
  }

  if (!requires_audit(decision)) {
    record_skip(detail_suffix + "/skipped");
    return WatchdogAuditWriteResult::skipped();
  }

  if (!audit_logger_) {
    record_failure(audit_write_fail_mapping.result_code,
                   detail_suffix + (options_.audit_required
                                        ? "/required_logger_unavailable"
                                        : "/logger_unavailable"));
    return WatchdogAuditWriteResult::failure(
        {},
        {},
        make_write_failure_outcome(audit_write_fail_mapping.result_code),
        audit_write_fail_mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::AuditWriteFail)) +
            ": watchdog audit bridge requires an audit::IAuditLogger sink before emit",
        "watchdog.audit",
        std::string(kWatchdogAuditBridgeSourceRef));
  }

  AuditEvent audit_event = make_audit_event(decision);
  AuditContext audit_context = make_audit_context();
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing,
                   detail_suffix + "/invalid_bridge_payload");
    return WatchdogAuditWriteResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "watchdog audit bridge produced an invalid audit payload",
        "watchdog.audit",
        std::string(kWatchdogAuditBridgeSourceRef));
  }

  const AuditWriteOutcome write_outcome =
      audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    record_failure(audit_write_fail_mapping.result_code,
                   detail_suffix + "/write_failed");
    return WatchdogAuditWriteResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        audit_write_fail_mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::AuditWriteFail)) +
            ": " + describe_write_failure(write_outcome),
        "watchdog.audit",
        std::string(kWatchdogAuditBridgeSourceRef));
  }

  ++emitted_total_;
  record_success(detail_suffix + (write_outcome.is_degraded_success()
                                      ? "/degraded_success"
                                      : "/success"));
  return WatchdogAuditWriteResult::success(std::move(audit_event),
                                           std::move(audit_context),
                                           write_outcome);
}

WatchdogAuditBridgeStatus WatchdogAuditBridge::get_status() const {
  return WatchdogAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .skipped_total = skipped_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

bool WatchdogAuditBridge::requires_audit(const TimeoutDecision& decision) {
  return decision.timeout_level == WatchdogTimeoutLevel::Critical ||
         decision.timeout_level == WatchdogTimeoutLevel::Fatal;
}

AuditEvent WatchdogAuditBridge::make_audit_event(
    const TimeoutDecision& decision) {
  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("watchdog.timeout_detected"),
      .actor = std::string(kWatchdogAuditActor),
      .target = std::string("watchdog_entity:") + decision.entity_id,
      .outcome = map_audit_outcome(decision.timeout_level),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = decision.evidence_ref,
      },
      .side_effects = std::vector<std::string>{
          make_side_effect("timeout_level",
                           std::string(watchdog_timeout_level_name(
                               decision.timeout_level))),
          make_side_effect("consecutive_miss",
                           std::to_string(decision.consecutive_miss)),
          make_side_effect("reason_code",
                           result_code_name(decision.reason_code)),
      },
      .timestamp = current_time_unix_ms(),
  };
}

AuditContext WatchdogAuditBridge::make_audit_context() const {
  return AuditContext{
      .request_id = std::string(kAuditContextUnknown),
      .session_id = std::string(kAuditContextUnknown),
      .trace_id = std::string(kAuditContextUnknown),
      .task_id = std::string(kAuditContextUnknown),
      .parent_task_id = std::string(kAuditContextUnknown),
      .lease_id = std::string(kAuditContextUnknown),
      .worker_type = std::string(kWatchdogAuditWorkerType),
  };
}

void WatchdogAuditBridge::record_success(std::string detail_suffix) {
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

void WatchdogAuditBridge::record_skip(std::string detail_suffix) {
  ++skipped_total_;
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

void WatchdogAuditBridge::record_failure(contracts::ResultCode result_code,
                                         std::string detail_suffix) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = options_.detail_ref_prefix + std::move(detail_suffix);
}

}  // namespace dasall::infra::watchdog
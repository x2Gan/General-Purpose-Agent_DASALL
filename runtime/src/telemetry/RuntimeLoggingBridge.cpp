#include "RuntimeLoggingBridge.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace dasall::runtime {
namespace {

[[nodiscard]] infra::LogLevel runtime_log_level(const RuntimeEventSeverity severity) {
  switch (severity) {
    case RuntimeEventSeverity::Debug:
      return infra::LogLevel::Debug;
    case RuntimeEventSeverity::Info:
      return infra::LogLevel::Info;
    case RuntimeEventSeverity::Warning:
      return infra::LogLevel::Warn;
    case RuntimeEventSeverity::Error:
      return infra::LogLevel::Error;
  }

  return infra::LogLevel::Info;
}

[[nodiscard]] std::string runtime_event_category_name(const RuntimeEventCategory category) {
  switch (category) {
    case RuntimeEventCategory::Transition:
      return "transition";
    case RuntimeEventCategory::BudgetReject:
      return "budget_reject";
    case RuntimeEventCategory::RecoveryReject:
      return "recovery_reject";
    case RuntimeEventCategory::SafeMode:
      return "safe_mode";
    case RuntimeEventCategory::Health:
      return "health";
    case RuntimeEventCategory::Maintenance:
      return "maintenance";
    case RuntimeEventCategory::Audit:
      return "audit";
  }

  return "audit";
}

[[nodiscard]] std::string runtime_event_severity_name(const RuntimeEventSeverity severity) {
  switch (severity) {
    case RuntimeEventSeverity::Debug:
      return "debug";
    case RuntimeEventSeverity::Info:
      return "info";
    case RuntimeEventSeverity::Warning:
      return "warning";
    case RuntimeEventSeverity::Error:
      return "error";
  }

  return "info";
}

[[nodiscard]] bool is_runtime_attr_allowlisted(std::string_view key) {
  static constexpr std::array<std::string_view, 11> kAllowedKeys = {
      "runtime_instance_id",
      "from_state",
      "to_state",
      "violation",
      "budget_type",
      "executed_action",
      "final_runtime_state",
      "previous_mode",
      "target_mode",
      "action",
      "selected_fallback",
  };

  return std::find(kAllowedKeys.begin(), kAllowedKeys.end(), key) != kAllowedKeys.end();
}

void add_context_attr(infra::LogEvent::AttributeMap* attrs,
                      std::string_view key,
                      const std::optional<std::string>& value) {
  if (value.has_value() && !value->empty()) {
    attrs->emplace(std::string(key), *value);
  }
}

[[nodiscard]] infra::LogEvent make_runtime_log_event(const RuntimeEventEnvelope& event) {
  infra::LogEvent log_event;
  log_event.level = runtime_log_level(event.severity);
  log_event.module = "runtime";
  log_event.message = event.event_name;
  log_event.ts = event.timestamp_ms;
  log_event.attrs.emplace("event_name", event.event_name);
  log_event.attrs.emplace("category", runtime_event_category_name(event.category));
  log_event.attrs.emplace("severity", runtime_event_severity_name(event.severity));
  add_context_attr(&log_event.attrs, "request_id", event.context.request_id);
  add_context_attr(&log_event.attrs, "session_id", event.context.session_id);
  add_context_attr(&log_event.attrs, "trace_id", event.context.trace_id);
  add_context_attr(&log_event.attrs, "turn_id", event.context.turn_id);
  add_context_attr(&log_event.attrs, "checkpoint_id", event.context.checkpoint_id);
  if (event.error_code.has_value()) {
    log_event.attrs.emplace("error_code",
                            std::to_string(static_cast<int>(*event.error_code)));
  }
  if (event.audit) {
    log_event.attrs.emplace("audit_ref_pending", "true");
  }
  for (const auto& attribute : event.attributes) {
    if (is_runtime_attr_allowlisted(attribute.key)) {
      log_event.attrs[attribute.key] = attribute.value;
    }
  }
  return log_event;
}

}  // namespace

RuntimeLoggingBridge::RuntimeLoggingBridge(
    std::shared_ptr<infra::logging::ILogger> logger)
    : logger_(std::move(logger)) {}

infra::logging::LogWriteResult RuntimeLoggingBridge::handle(
    const RuntimeEventEnvelope& event) const {
  if (logger_ == nullptr) {
    return infra::logging::LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "runtime logger unavailable",
        "runtime.logging_bridge.handle",
        "RuntimeLoggingBridge");
  }
  if (!event.has_minimum_fields()) {
    return infra::logging::LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "runtime event envelope missing event_name",
        "runtime.logging_bridge.handle",
        "RuntimeLoggingBridge");
  }

  return logger_->log(make_runtime_log_event(event));
}

}  // namespace dasall::runtime
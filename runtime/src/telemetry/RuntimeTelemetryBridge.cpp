#include "RuntimeTelemetryBridge.h"

#include <chrono>
#include <utility>

#include "../safety/SafeModeController.h"
#include "budget/BudgetDecision.h"
#include "checkpoint/RecoveryOutcome.h"

namespace dasall::runtime {
namespace {

[[nodiscard]] std::string budget_type_name(
    const std::optional<contracts::BudgetType>& budget_type) {
  if (!budget_type.has_value()) {
    return "unknown";
  }

  switch (*budget_type) {
    case contracts::BudgetType::Token:
      return "token";
    case contracts::BudgetType::Turn:
      return "turn";
    case contracts::BudgetType::ToolCall:
      return "tool_call";
    case contracts::BudgetType::Latency:
      return "latency";
    case contracts::BudgetType::Replan:
      return "replan";
  }

  return "unknown";
}

[[nodiscard]] std::string safe_mode_action_name(const SafeModeAction action) {
  switch (action) {
    case SafeModeAction::None:
      return "none";
    case SafeModeAction::EnterFailedSafe:
      return "enter_failed_safe";
    case SafeModeAction::EnterDegraded:
      return "enter_degraded";
    case SafeModeAction::EnterSafeMode:
      return "enter_safe_mode";
    case SafeModeAction::ExitToNormal:
      return "exit_to_normal";
  }

  return "none";
}

[[nodiscard]] RuntimeEventContext to_event_context(const RuntimeTelemetryContext& context) {
  return RuntimeEventContext{
      .request_id = context.request_id,
      .session_id = context.session_id,
      .trace_id = context.trace_id,
      .turn_id = context.turn_id,
      .checkpoint_id = context.checkpoint_id,
  };
}

void append_attribute(RuntimeEventEnvelope* envelope,
                      const std::string& key,
                      const std::string& value) {
  if (key.empty()) {
    return;
  }

  envelope->attributes.push_back(RuntimeEventAttribute{
      .key = key,
      .value = value,
  });
}

[[nodiscard]] std::optional<RuntimeErrorCode> recovery_error_code(
    const contracts::RecoveryOutcome& outcome) {
  if (outcome.rejection_reason.has_value()) {
    return RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED;
  }

  if (outcome.escalation_reason.has_value() ||
      outcome.final_runtime_state == std::optional<std::string>("FailedSafe") ||
      outcome.final_runtime_state == std::optional<std::string>("SafeMode")) {
    return RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED;
  }

  return std::nullopt;
}

}  // namespace

RuntimeTelemetryBridge::RuntimeTelemetryBridge(
    std::shared_ptr<RuntimeEventBus> event_bus,
    RuntimeTelemetryBridgeOptions options)
    : event_bus_(std::move(event_bus)), options_(std::move(options)) {
  if (!options_.now_ms) {
    options_.now_ms = []() { return default_now_ms(); };
  }
}

RuntimeTelemetryRecord RuntimeTelemetryBridge::emit_transition(
    const RuntimeState from_state,
    const RuntimeState to_state,
    const RuntimeTelemetryContext& context,
    std::string detail,
    const std::optional<RuntimeErrorCode> error_code) {
  RuntimeTelemetryRecord record{
      .kind = RuntimeTelemetryKind::Transition,
      .envelope = RuntimeEventEnvelope{
          .sequence = 0U,
          .category = RuntimeEventCategory::Transition,
          .severity = error_code.has_value() ? RuntimeEventSeverity::Warning
                                             : RuntimeEventSeverity::Info,
          .event_name = "runtime.transition",
          .detail = detail.empty()
                        ? std::string(runtime_state_name(from_state)) + " -> " +
                              runtime_state_name(to_state)
                        : std::move(detail),
          .context = to_event_context(context),
          .error_code = error_code,
          .attributes = {},
          .audit = false,
          .timestamp_ms = options_.now_ms(),
      },
      .subject = std::string(runtime_state_name(from_state)) + "->" +
                 runtime_state_name(to_state),
  };
  append_attribute(&record.envelope, "from_state", runtime_state_name(from_state));
  append_attribute(&record.envelope, "to_state", runtime_state_name(to_state));
  append_common_attributes(&record.envelope);
  return store_and_publish(std::move(record));
}

RuntimeTelemetryRecord RuntimeTelemetryBridge::emit_budget_reject(
    const BudgetDecision& decision,
    const RuntimeTelemetryContext& context,
    std::string detail) {
  RuntimeTelemetryRecord record{
      .kind = RuntimeTelemetryKind::BudgetReject,
      .envelope = RuntimeEventEnvelope{
          .sequence = 0U,
          .category = RuntimeEventCategory::BudgetReject,
          .severity = RuntimeEventSeverity::Warning,
          .event_name = "runtime.budget.reject",
          .detail = detail.empty() ? decision.detail : std::move(detail),
          .context = to_event_context(context),
          .error_code = decision.error_code,
          .attributes = {},
          .audit = true,
          .timestamp_ms = options_.now_ms(),
      },
      .subject = budget_violation_name(decision.violation),
  };
  append_attribute(&record.envelope, "violation", budget_violation_name(decision.violation));
  append_attribute(&record.envelope, "budget_type", budget_type_name(decision.budget_type));
  append_common_attributes(&record.envelope);
  return store_and_publish(std::move(record));
}

RuntimeTelemetryRecord RuntimeTelemetryBridge::emit_recovery_reject(
    const contracts::RecoveryOutcome& outcome,
    const RuntimeTelemetryContext& context,
    std::string detail) {
  RuntimeTelemetryRecord record{
      .kind = RuntimeTelemetryKind::RecoveryReject,
      .envelope = RuntimeEventEnvelope{
          .sequence = 0U,
          .category = RuntimeEventCategory::RecoveryReject,
          .severity = RuntimeEventSeverity::Error,
          .event_name = "runtime.recovery.reject",
          .detail = detail.empty()
                        ? outcome.rejection_reason.value_or(
                              outcome.escalation_reason.value_or(
                                  std::string("recovery reject")))
                        : std::move(detail),
          .context = to_event_context(context),
          .error_code = recovery_error_code(outcome),
          .attributes = {},
          .audit = true,
          .timestamp_ms = options_.now_ms(),
      },
      .subject = outcome.executed_action.value_or(std::string("reject")),
  };
  append_attribute(&record.envelope,
                   "executed_action",
                   outcome.executed_action.value_or(std::string("none")));
  append_attribute(&record.envelope,
                   "final_runtime_state",
                   outcome.final_runtime_state.value_or(std::string("unknown")));
  append_attribute(&record.envelope,
                   "checkpoint_ref",
                   outcome.checkpoint_ref.value_or(std::string("")));
  append_common_attributes(&record.envelope);
  return store_and_publish(std::move(record));
}

RuntimeTelemetryRecord RuntimeTelemetryBridge::emit_safe_mode(
    const SafeModeDecision& decision,
    const RuntimeTelemetryContext& context,
    std::string detail) {
  RuntimeTelemetryRecord record{
      .kind = RuntimeTelemetryKind::SafeMode,
      .envelope = RuntimeEventEnvelope{
          .sequence = 0U,
          .category = RuntimeEventCategory::SafeMode,
          .severity = decision.target_mode == SafeModeState::Degraded
                          ? RuntimeEventSeverity::Warning
                          : RuntimeEventSeverity::Error,
          .event_name = "runtime.safe_mode",
          .detail = detail.empty() ? decision.detail : std::move(detail),
          .context = to_event_context(context),
          .error_code = decision.error_code,
          .attributes = {},
          .audit = true,
          .timestamp_ms = options_.now_ms(),
      },
      .subject = safe_mode_state_name(decision.target_mode),
  };
  append_attribute(&record.envelope,
                   "previous_mode",
                   safe_mode_state_name(decision.previous_mode));
  append_attribute(&record.envelope,
                   "target_mode",
                   safe_mode_state_name(decision.target_mode));
  append_attribute(&record.envelope,
                   "action",
                   safe_mode_action_name(decision.action));
  append_attribute(&record.envelope,
                   "selected_fallback",
                   decision.selected_fallback.value_or(std::string("")));
  append_common_attributes(&record.envelope);
  return store_and_publish(std::move(record));
}

std::vector<RuntimeTelemetryRecord> RuntimeTelemetryBridge::snapshot() const {
  const std::lock_guard<std::mutex> lock(records_mutex_);
  return records_;
}

std::size_t RuntimeTelemetryBridge::emit_count() const {
  const std::lock_guard<std::mutex> lock(records_mutex_);
  return records_.size();
}

std::int64_t RuntimeTelemetryBridge::default_now_ms() {
  using Clock = std::chrono::system_clock;
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

RuntimeTelemetryRecord RuntimeTelemetryBridge::store_and_publish(
    RuntimeTelemetryRecord record) {
  if (event_bus_ != nullptr) {
    (void)event_bus_->publish(record.envelope);
  }

  const std::lock_guard<std::mutex> lock(records_mutex_);
  records_.push_back(record);
  return records_.back();
}

void RuntimeTelemetryBridge::append_common_attributes(RuntimeEventEnvelope* envelope) const {
  append_attribute(envelope,
                   "runtime_instance_id",
                   options_.runtime_instance_id.empty() ? std::string("runtime")
                                                        : options_.runtime_instance_id);
}

}  // namespace dasall::runtime
#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "budget/BudgetDecision.h"
#include "checkpoint/RecoveryOutcome.h"
#include "safety/SafeModeController.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"
#include "telemetry/RuntimeTelemetryBridge.h"

namespace {

using dasall::contracts::RecoveryOutcome;
using dasall::runtime::BudgetViolationClass;
using dasall::runtime::RuntimeErrorCode;
using dasall::runtime::RuntimeEventBus;
using dasall::runtime::RuntimeEventBusOptions;
using dasall::runtime::RuntimeEventEnvelope;
using dasall::runtime::RuntimeState;
using dasall::runtime::RuntimeTelemetryBridge;
using dasall::runtime::RuntimeTelemetryBridgeOptions;
using dasall::runtime::RuntimeTelemetryContext;
using dasall::runtime::SafeModeAction;
using dasall::runtime::SafeModeDecision;
using dasall::runtime::SafeModeState;

[[nodiscard]] RuntimeTelemetryContext make_context() {
  return RuntimeTelemetryContext{
      .request_id = std::string("req-023"),
      .session_id = std::string("session-023"),
      .trace_id = std::string("trace-023"),
      .turn_id = std::string("turn-023"),
      .checkpoint_id = std::string("chk-023"),
  };
}

[[nodiscard]] bool has_attribute(const RuntimeEventEnvelope& envelope,
                                 const std::string& key,
                                 const std::string& value) {
  return std::find_if(
             envelope.attributes.begin(),
             envelope.attributes.end(),
             [&key, &value](const auto& attribute) {
               return attribute.key == key && attribute.value == value;
             }) != envelope.attributes.end();
}

void test_telemetry_bridge_emits_transition_and_budget_reject_with_correlation_fields() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto event_bus = std::make_shared<RuntimeEventBus>(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 8U,
      .now_ms = []() { return 1700000001000LL; },
  });
  RuntimeTelemetryBridge bridge(
      event_bus,
      RuntimeTelemetryBridgeOptions{
          .runtime_instance_id = "runtime-023",
          .now_ms = []() { return 1700000001001LL; },
      });

  std::vector<RuntimeEventEnvelope> delivered;
  const auto subscription = event_bus->subscribe([&delivered](const RuntimeEventEnvelope& event) {
    delivered.push_back(event);
  });
  assert_true(subscription.is_valid(), "event bus subscription should be valid");

  const auto transition_record = bridge.emit_transition(
      RuntimeState::Planning,
      RuntimeState::Reasoning,
      make_context(),
      "planner advanced to reasoning");
  const auto budget_record = bridge.emit_budget_reject(
      dasall::runtime::make_budget_rejected_decision(
          BudgetViolationClass::LatencyExhausted,
          "latency budget exhausted"),
      make_context());

  assert_equal(2,
               static_cast<int>(event_bus->dispatch_pending()),
               "event bus should dispatch both telemetry events");
  assert_equal(2,
               static_cast<int>(bridge.emit_count()),
               "bridge should retain both emitted telemetry records");
  assert_equal(std::string("Planning->Reasoning"),
               transition_record.subject,
               "transition record should summarize the from->to state path");
  assert_true(budget_record.envelope.error_code == RuntimeErrorCode::RT_E_303_LATENCY_OVERRUN,
              "budget reject should preserve the RT_E_* reject code");
  assert_equal(std::string("trace-023"),
               delivered.front().context.trace_id.value_or(std::string()),
               "telemetry events should propagate trace_id correlation fields");
  assert_true(has_attribute(delivered.back(), "runtime_instance_id", "runtime-023"),
              "telemetry events should carry runtime_instance_id as a common attribute");
}

void test_telemetry_bridge_emits_recovery_and_safe_mode_as_audit_events() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto event_bus = std::make_shared<RuntimeEventBus>(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 4U,
      .now_ms = []() { return 1700000001100LL; },
  });
  RuntimeTelemetryBridge bridge(event_bus);

  const auto recovery_record = bridge.emit_recovery_reject(
      RecoveryOutcome{
          .executed_action = std::string("abort_safe"),
          .final_runtime_state = std::string("FailedSafe"),
          .updated_retry_count = 2U,
          .checkpoint_ref = std::string("chk-023-r"),
          .compensation_result_ref = std::nullopt,
          .rejection_reason = std::string("retry budget exhausted"),
          .escalation_reason = std::nullopt,
      },
      make_context());
  const auto safe_mode_record = bridge.emit_safe_mode(
      SafeModeDecision{
          .transition_required = true,
          .previous_mode = SafeModeState::Normal,
          .target_mode = SafeModeState::SafeMode,
          .action = SafeModeAction::EnterSafeMode,
          .target_runtime_state = RuntimeState::SafeMode,
          .error_code = RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
          .selected_fallback = std::nullopt,
          .detail = "watchdog timeout triggered safe mode",
      },
      make_context());

  const auto delivered_count = event_bus->dispatch_pending();
  assert_equal(2,
               static_cast<int>(delivered_count),
               "event bus should dispatch the recovery and safe-mode audit events");
  assert_true(recovery_record.envelope.audit,
              "recovery reject should be emitted as an audit event");
  assert_true(recovery_record.envelope.error_code == RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
              "recovery rejection should map to RT_E_500_RECOVERY_REJECTED");
  assert_true(safe_mode_record.envelope.audit,
              "safe-mode transitions should be emitted as audit events");
  assert_true(has_attribute(safe_mode_record.envelope, "target_mode", "SafeMode"),
              "safe-mode telemetry should preserve target_mode attributes");
}

}  // namespace

int main() {
  try {
    test_telemetry_bridge_emits_transition_and_budget_reject_with_correlation_fields();
    test_telemetry_bridge_emits_recovery_and_safe_mode_as_audit_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
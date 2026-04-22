#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"
#include "budget/BudgetDecision.h"
#include "safety/SafeModeController.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"
#include "telemetry/RuntimeTelemetryBridge.h"

namespace {

using dasall::profiles::ProfileCatalog;
using dasall::profiles::RuntimePolicyLoadRequest;
using dasall::profiles::RuntimePolicyProvider;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::runtime::BudgetViolationClass;
using dasall::runtime::HealthSignal;
using dasall::runtime::RuntimeErrorCode;
using dasall::runtime::RuntimeEventBus;
using dasall::runtime::RuntimeEventBusOptions;
using dasall::runtime::RuntimeEventEnvelope;
using dasall::runtime::RuntimeTelemetryBridge;
using dasall::runtime::RuntimeTelemetryBridgeOptions;
using dasall::runtime::RuntimeTelemetryContext;
using dasall::runtime::SafeModeController;
using dasall::runtime::SafeModeState;
using dasall::runtime::SafeModeTrigger;
using dasall::runtime::SafeModeTriggerKind;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::shared_ptr<const RuntimePolicySnapshot> load_snapshot(
    const std::string& profile_id) {
  const ProfileCatalog catalog(repository_root() / "profiles");
  const RuntimePolicyProvider provider(catalog);
  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = profile_id,
  });
  assert_true(runtime_result.ok(),
              "runtime safe-mode integration should load snapshot for " + profile_id);
  assert_true(runtime_result.snapshot->has_consistent_values(),
              "runtime safe-mode integration should keep snapshot values consistent for " +
                  profile_id);
  return runtime_result.snapshot;
}

[[nodiscard]] RuntimeTelemetryContext make_context(
    const std::string& request_id,
    const std::string& session_id) {
  return RuntimeTelemetryContext{
      .request_id = request_id,
      .session_id = session_id,
      .trace_id = request_id + "-trace",
      .turn_id = request_id + "-turn",
      .checkpoint_id = request_id + "-checkpoint",
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

void test_safe_mode_transitions_publish_auditable_terminal_states_from_real_profiles() {
  auto event_bus = std::make_shared<RuntimeEventBus>(RuntimeEventBusOptions{
      .max_non_audit_queue_depth = 8U,
      .now_ms = []() { return 1700000004000LL; },
  });
  RuntimeTelemetryBridge bridge(
      event_bus,
      RuntimeTelemetryBridgeOptions{
          .runtime_instance_id = "runtime-030",
          .now_ms = []() { return 1700000004001LL; },
      });

  std::vector<RuntimeEventEnvelope> delivered;
  const auto subscription = event_bus->subscribe(
      [&delivered](const RuntimeEventEnvelope& event) { delivered.push_back(event); });
  assert_true(subscription.is_valid(),
              "safe-mode integration should subscribe to the runtime event bus");

  const auto desktop_snapshot = load_snapshot("desktop_full");
  assert_true(desktop_snapshot->execution_policy().safe_mode_enabled,
              "desktop_full should keep safe mode enabled");
  assert_true(desktop_snapshot->degrade_policy().allow_budget_degrade,
              "desktop_full should allow budget degrade");

  SafeModeController budget_controller(desktop_snapshot);
  const auto budget_decision = budget_controller.evaluate_entry(
      SafeModeTrigger{
          .trigger_kind = SafeModeTriggerKind::BudgetExhausted,
          .budget_decision = dasall::runtime::make_budget_rejected_decision(
              BudgetViolationClass::LatencyExhausted,
              "latency budget exhausted during tool round"),
          .recovery_outcome = std::nullopt,
          .error_code = std::nullopt,
          .health_signal = std::nullopt,
          .detail = "latency budget exhausted during tool round",
      });
  assert_true(budget_decision.target_mode == SafeModeState::Degraded,
              "budget overrun should enter degraded mode under desktop_full profile");
  assert_true(budget_decision.error_code == RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
              "budget overrun should map to RT_E_511_DEGRADE_ENTERED");
  const auto budget_record = bridge.emit_safe_mode(
      budget_decision,
      make_context("req-030-budget", "session-030-budget"));

  SafeModeController watchdog_controller(desktop_snapshot);
  const auto watchdog_decision = watchdog_controller.evaluate_entry(
      SafeModeTrigger{
          .trigger_kind = SafeModeTriggerKind::WatchdogTimeout,
          .budget_decision = std::nullopt,
          .recovery_outcome = std::nullopt,
          .error_code = std::nullopt,
          .health_signal = std::nullopt,
          .detail = "watchdog timed out while request remained active",
      });
  assert_true(watchdog_decision.target_mode == SafeModeState::SafeMode,
              "watchdog timeout should force safe mode");
  assert_true(watchdog_decision.error_code == RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
              "watchdog timeout should map to RT_E_510_SAFE_MODE_ENTERED");
  const auto watchdog_record = bridge.emit_safe_mode(
      watchdog_decision,
      make_context("req-030-watchdog", "session-030-watchdog"));

  SafeModeController dependency_failover_controller(desktop_snapshot);
  const auto dependency_failover_decision = dependency_failover_controller.evaluate_entry(
      SafeModeTrigger{
          .trigger_kind = SafeModeTriggerKind::DependencyUnavailable,
          .budget_decision = std::nullopt,
          .recovery_outcome = std::nullopt,
          .error_code = RuntimeErrorCode::RT_E_102_DEPENDENCY_UNAVAILABLE,
          .health_signal = HealthSignal{
              .dependency_available = false,
              .watchdog_healthy = true,
              .dependency_name = "llm_cloud_adapter",
              .detail = "llm cloud adapter unavailable",
          },
          .detail = "llm cloud adapter unavailable",
      });
  assert_true(dependency_failover_decision.target_mode == SafeModeState::Degraded,
              "dependency outage with model failover should enter degraded mode");
  assert_equal(std::string("lan.general"),
               dependency_failover_decision.selected_fallback.value_or(std::string()),
               "dependency outage should record the first real fallback route");
  const auto dependency_failover_record = bridge.emit_safe_mode(
      dependency_failover_decision,
      make_context("req-030-dependency-failover", "session-030-dependency-failover"));

  const auto edge_minimal_snapshot = load_snapshot("edge_minimal");
  assert_true(!edge_minimal_snapshot->degrade_policy().allow_model_failover,
              "edge_minimal should disable model failover");

  SafeModeController dependency_failed_safe_controller(edge_minimal_snapshot);
  const auto dependency_failed_safe_decision = dependency_failed_safe_controller.evaluate_entry(
      SafeModeTrigger{
          .trigger_kind = SafeModeTriggerKind::DependencyUnavailable,
          .budget_decision = std::nullopt,
          .recovery_outcome = std::nullopt,
          .error_code = RuntimeErrorCode::RT_E_102_DEPENDENCY_UNAVAILABLE,
          .health_signal = HealthSignal{
              .dependency_available = false,
              .watchdog_healthy = true,
              .dependency_name = "llm_cloud_adapter",
              .detail = "llm cloud adapter unavailable",
          },
          .detail = "llm cloud adapter unavailable",
      });
  assert_true(dependency_failed_safe_decision.target_mode == SafeModeState::FailedSafe,
              "dependency outage without failover should enter failed-safe mode");
  assert_true(dependency_failed_safe_decision.error_code == RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED,
              "dependency outage should map to RT_E_501_RECOVERY_ESCALATED");
  const auto dependency_failed_safe_record = bridge.emit_safe_mode(
      dependency_failed_safe_decision,
      make_context("req-030-dependency-failed-safe", "session-030-dependency-failed-safe"));

  assert_equal(4,
               static_cast<int>(event_bus->dispatch_pending()),
               "event bus should dispatch all safe-mode audit events");
  assert_equal(4,
               static_cast<int>(delivered.size()),
               "safe-mode integration should publish four audit records");
  assert_true(std::all_of(
                  delivered.begin(),
                  delivered.end(),
                  [](const RuntimeEventEnvelope& event) {
                    return event.event_name == "runtime.safe_mode" && event.audit;
                  }),
              "safe-mode integration should mark all terminal state events as audit records");
  assert_true(has_attribute(budget_record.envelope, "target_mode", "Degraded"),
              "budget safe-mode event should preserve target_mode=Degraded");
  assert_true(has_attribute(watchdog_record.envelope, "target_mode", "SafeMode"),
              "watchdog safe-mode event should preserve target_mode=SafeMode");
  assert_true(has_attribute(dependency_failover_record.envelope, "target_mode", "Degraded"),
              "dependency failover event should preserve target_mode=Degraded");
  assert_true(has_attribute(dependency_failed_safe_record.envelope, "target_mode", "FailedSafe"),
              "dependency failed-safe event should preserve target_mode=FailedSafe");
  assert_equal(std::string("req-030-budget"),
               delivered.front().context.request_id.value_or(std::string()),
               "safe-mode event should preserve telemetry correlation fields");
}

}  // namespace

int main() {
  try {
    test_safe_mode_transitions_publish_auditable_terminal_states_from_real_profiles();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
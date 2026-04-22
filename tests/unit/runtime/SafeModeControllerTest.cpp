#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "safety/SafeModeController.h"
#include "support/TestAssertions.h"

namespace {

std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> make_policy_snapshot(
    const bool allow_model_failover,
    const bool allow_budget_degrade,
    const std::vector<std::string>& fallback_chain,
    const bool safe_mode_enabled = true) {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return std::make_shared<RuntimePolicySnapshot>(
      22U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 4U,
          .max_latency_ms = 2500U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"default",
               ModelRoutePolicy{
                   .route = "gpt-main",
                   .fallback_route = std::string("gpt-fallback"),
                   .streaming_enabled = false,
               }},
          },
      },
      TokenBudgetPolicy{
          .max_input_tokens = 2048U,
          .max_output_tokens = 1024U,
          .max_history_turns = 8U,
          .compression_threshold = 512U,
      },
      PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"default"},
      },
      CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 5000,
          .stale_read_allowed = true,
          .failure_backoff_ms = 200,
      },
      DegradePolicy{
          .fallback_chain = fallback_chain,
          .allow_model_failover = allow_model_failover,
          .allow_budget_degrade = allow_budget_degrade,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1500, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .tool = TimeoutBudget{.timeout_ms = 1200, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .mcp = TimeoutBudget{.timeout_ms = 1800, .retry_budget = 2U, .circuit_breaker_threshold = 3U},
          .workflow = TimeoutBudget{.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = safe_mode_enabled,
          .audit_level = "strict",
          .allowed_tool_domains = {"default"},
      },
      OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "manual",
      });
}

}  // namespace

int main() {
  using dasall::runtime::BudgetViolationClass;
  using dasall::runtime::RuntimeErrorCode;
  using dasall::runtime::SafeModeAction;
  using dasall::runtime::SafeModeController;
  using dasall::runtime::SafeModeExitRequest;
  using dasall::runtime::SafeModeState;
  using dasall::runtime::SafeModeTrigger;
  using dasall::runtime::SafeModeTriggerKind;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    SafeModeController budget_controller(make_policy_snapshot(
        false,
        true,
        {"allow_budget_degrade", "abort_safe"}));
    const auto budget_decision = budget_controller.evaluate_entry(
        SafeModeTrigger{
            .trigger_kind = SafeModeTriggerKind::BudgetExhausted,
            .budget_decision = dasall::runtime::make_budget_rejected_decision(
                BudgetViolationClass::LatencyExhausted,
                "latency budget exhausted"),
            .recovery_outcome = std::nullopt,
            .error_code = std::nullopt,
            .health_signal = std::nullopt,
            .detail = "latency budget exhausted",
        });
    assert_true(budget_decision.transition_required,
                "budget exhaustion should require a safe-mode transition");
    assert_true(budget_decision.target_mode == SafeModeState::Degraded,
                "budget degrade should enter Degraded mode when allowed");
    assert_true(budget_decision.action == SafeModeAction::EnterDegraded,
                "budget degrade should emit EnterDegraded action");
    assert_equal("allow_budget_degrade",
                 budget_decision.selected_fallback.value_or(std::string()),
                 "budget degrade should record selected fallback chain step");
    assert_true(budget_decision.error_code == RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
                "budget degrade should map to RT_E_511_DEGRADE_ENTERED");
    assert_true(budget_controller.current_mode() == SafeModeState::Degraded,
                "controller should retain Degraded current mode after entry");

    const auto recovered_exit = budget_controller.evaluate_exit(
        SafeModeExitRequest{
            .dependencies_healthy = true,
            .watchdog_healthy = true,
            .operator_cleared = true,
            .budget_restored = true,
            .detail = "operator cleared degraded mode",
        });
    assert_true(recovered_exit.transition_required,
                "healthy exit request should return controller to Normal mode");
    assert_true(recovered_exit.target_mode == SafeModeState::Normal,
                "healthy exit should target Normal mode");
    assert_true(budget_controller.current_mode() == SafeModeState::Normal,
                "controller should retain Normal mode after successful exit");

    SafeModeController dependency_controller(make_policy_snapshot(
        false,
        false,
        {"allow_model_failover", "abort_safe"}));
    const auto dependency_decision = dependency_controller.evaluate_entry(
        SafeModeTrigger{
            .trigger_kind = SafeModeTriggerKind::DependencyUnavailable,
            .budget_decision = std::nullopt,
            .recovery_outcome = std::nullopt,
            .error_code = std::nullopt,
            .health_signal = dasall::runtime::HealthSignal{
                .dependency_available = false,
                .watchdog_healthy = true,
                .dependency_name = "llm",
                .detail = "llm adapter unavailable",
            },
            .detail = "llm adapter unavailable",
        });
    assert_true(dependency_decision.target_mode == SafeModeState::FailedSafe,
                "dependency outage should fall back to FailedSafe when no degrade path is enabled");
    assert_equal("abort_safe",
                 dependency_decision.selected_fallback.value_or(std::string()),
                 "dependency outage should record abort_safe as final fallback");
    assert_true(dependency_decision.error_code == RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED,
                "failed-safe transition should map to RT_E_501_RECOVERY_ESCALATED");

    SafeModeController policy_controller(make_policy_snapshot(
        true,
        true,
        {"allow_model_failover", "allow_budget_degrade", "abort_safe"},
        true));
    const auto policy_decision = policy_controller.evaluate_entry(
        SafeModeTrigger{
            .trigger_kind = SafeModeTriggerKind::PolicyForbidden,
            .budget_decision = std::nullopt,
            .recovery_outcome = std::nullopt,
            .error_code = RuntimeErrorCode::RT_E_102_DEPENDENCY_UNAVAILABLE,
            .health_signal = std::nullopt,
            .detail = "profile policy forbids continuing execution",
        });
    assert_true(policy_decision.target_mode == SafeModeState::SafeMode,
                "policy forbidden should force SafeMode");
    assert_true(policy_decision.error_code == RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
                "policy forbidden should map to RT_E_510_SAFE_MODE_ENTERED");

    const auto blocked_exit = policy_controller.evaluate_exit(
        SafeModeExitRequest{
            .dependencies_healthy = true,
            .watchdog_healthy = false,
            .operator_cleared = true,
            .budget_restored = true,
            .detail = "watchdog still unhealthy",
        });
    assert_true(!blocked_exit.transition_required,
                "exit should be rejected while watchdog health is still red");
    assert_true(policy_controller.current_mode() == SafeModeState::SafeMode,
                "controller should stay in SafeMode after rejected exit");

    SafeModeController recovery_controller(make_policy_snapshot(
        true,
        true,
        {"allow_model_failover", "allow_budget_degrade", "abort_safe"}));
    const auto recovery_decision = recovery_controller.evaluate_entry(
        SafeModeTrigger{
            .trigger_kind = SafeModeTriggerKind::RecoveryExhausted,
            .budget_decision = std::nullopt,
            .recovery_outcome = dasall::contracts::RecoveryOutcome{
                .executed_action = std::string("degrade"),
                .final_runtime_state = std::string("Degraded"),
                .updated_retry_count = 2U,
                .checkpoint_ref = std::string("chk-022"),
                .compensation_result_ref = std::nullopt,
                .rejection_reason = std::nullopt,
                .escalation_reason = std::string("recovery escalated into degraded mode"),
            },
            .error_code = std::nullopt,
            .health_signal = std::nullopt,
            .detail = "recovery escalated into degraded mode",
        });
    assert_true(recovery_decision.target_mode == SafeModeState::Degraded,
                "degrade recovery outcome should enter Degraded mode");
    assert_equal("degrade",
                 recovery_decision.selected_fallback.value_or(std::string()),
                 "recovery degrade path should preserve executed action as fallback evidence");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
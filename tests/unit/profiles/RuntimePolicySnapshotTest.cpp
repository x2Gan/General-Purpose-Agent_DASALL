#include <exception>
#include <iostream>
#include <type_traits>

#include "RuntimePolicySnapshot.h"
#include "support/TestAssertions.h"

namespace {

dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
    using dasall::profiles::MemoryMaintenancePolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return RuntimePolicySnapshot{
      7U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 24U,
          .max_tool_calls = 16U,
          .max_latency_ms = 5000U,
          .max_replan_count = 3U,
      },
      ModelProfile{
          .stage_routes = {
              {"default",
               ModelRoutePolicy{
                   .route = "gpt-main",
                   .fallback_route = std::string("gpt-fallback"),
                   .streaming_enabled = true,
               }},
          },
      },
      TokenBudgetPolicy{
          .max_input_tokens = 4096U,
          .max_output_tokens = 2048U,
          .max_history_turns = 12U,
          .compression_threshold = 1024U,
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
          .fallback_chain = {"local", "safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1500, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .tool = TimeoutBudget{.timeout_ms = 1200, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .mcp = TimeoutBudget{.timeout_ms = 1800, .retry_budget = 2U, .circuit_breaker_threshold = 3U},
          .workflow = TimeoutBudget{.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "strict",
          .allowed_tool_domains = {"default"},
      },
      OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "manual",
      },
      1U,
      false,
      MemoryMaintenancePolicy{
          .enabled = true,
          .interval_ms = 45000,
          .jitter_ms = 5000,
          .retention_ms = 240000,
          .checkpoint_strategy = "passive_each_tick",
      },
  };
}

void test_runtime_policy_snapshot_exposes_read_only_policy_fields() {
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const RuntimePolicySnapshot snapshot = make_snapshot();

  assert_true(snapshot.has_consistent_values(),
              "runtime policy snapshot should accept a complete immutable policy set");
  assert_true(snapshot.generation() == 7U,
              "runtime policy snapshot should preserve generation number");
  assert_equal(std::string("desktop_full"), snapshot.effective_profile_id(),
               "runtime policy snapshot should preserve effective profile id");
  assert_true(snapshot.model_profile().stage_routes.contains("default"),
              "runtime policy snapshot should preserve model route map");
  assert_true(snapshot.timeout_policy().workflow.timeout_ms == 2500,
              "runtime policy snapshot should preserve workflow timeout policy");
    assert_true(snapshot.memory_maintenance_policy().interval_ms == 45000,
                            "runtime policy snapshot should preserve memory maintenance cadence policy");
}

void test_runtime_policy_snapshot_interface_is_const_only() {
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<const RuntimePolicySnapshot&>().runtime_budget()),
                               const dasall::contracts::RuntimeBudget&>,
                "runtime_budget getter should expose const reference only");
  static_assert(std::is_same_v<decltype(std::declval<const RuntimePolicySnapshot&>().model_profile()),
                               const dasall::profiles::ModelProfile&>,
                "model_profile getter should expose const reference only");
  static_assert(std::is_same_v<decltype(std::declval<const RuntimePolicySnapshot&>().ops_policy()),
                               const dasall::profiles::OpsPolicy&>,
                "ops_policy getter should expose const reference only");
    static_assert(
            std::is_same_v<decltype(std::declval<const RuntimePolicySnapshot&>().memory_maintenance_policy()),
                                         const dasall::profiles::MemoryMaintenancePolicy&>,
            "memory_maintenance_policy getter should expose const reference only");

  assert_true(std::is_constructible_v<RuntimePolicySnapshot,
                                      std::uint64_t,
                                      std::string,
                                      dasall::contracts::RuntimeBudget,
                                      dasall::profiles::ModelProfile,
                                      dasall::profiles::TokenBudgetPolicy,
                                      dasall::profiles::PromptPolicy,
                                      dasall::profiles::CapabilityCachePolicy,
                                      dasall::profiles::DegradePolicy,
                                      dasall::profiles::TimeoutPolicy,
                                      dasall::profiles::ExecutionPolicy,
                                      dasall::profiles::OpsPolicy>,
              "runtime policy snapshot should remain constructible from the frozen field set");
}

void test_runtime_policy_snapshot_rejects_incomplete_budget_or_policy_domains() {
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::tests::support::assert_true;

  RuntimePolicySnapshot missing_budget = make_snapshot();
  RuntimePolicySnapshot duplicate_prompt_rules = make_snapshot();

  auto invalid_budget = missing_budget.runtime_budget();
  invalid_budget.max_latency_ms = std::nullopt;

  auto invalid_prompt_policy = duplicate_prompt_rules.prompt_policy();
  invalid_prompt_policy.trusted_sources = {"profiles", "profiles"};
    auto invalid_memory_policy = duplicate_prompt_rules.memory_maintenance_policy();
    invalid_memory_policy.checkpoint_strategy = "not-supported";

  const RuntimePolicySnapshot rebuilt_missing_budget{
      missing_budget.generation(),
      missing_budget.effective_profile_id(),
      invalid_budget,
      missing_budget.model_profile(),
      missing_budget.token_budget_policy(),
      missing_budget.prompt_policy(),
      missing_budget.capability_cache_policy(),
      missing_budget.degrade_policy(),
      missing_budget.timeout_policy(),
      missing_budget.execution_policy(),
      missing_budget.ops_policy(),
  };

  const RuntimePolicySnapshot rebuilt_duplicate_prompt_rules{
      duplicate_prompt_rules.generation(),
      duplicate_prompt_rules.effective_profile_id(),
      duplicate_prompt_rules.runtime_budget(),
      duplicate_prompt_rules.model_profile(),
      duplicate_prompt_rules.token_budget_policy(),
      invalid_prompt_policy,
      duplicate_prompt_rules.capability_cache_policy(),
      duplicate_prompt_rules.degrade_policy(),
      duplicate_prompt_rules.timeout_policy(),
      duplicate_prompt_rules.execution_policy(),
      duplicate_prompt_rules.ops_policy(),
  };

  const RuntimePolicySnapshot rebuilt_invalid_memory_policy{
      duplicate_prompt_rules.generation(),
      duplicate_prompt_rules.effective_profile_id(),
      duplicate_prompt_rules.runtime_budget(),
      duplicate_prompt_rules.model_profile(),
      duplicate_prompt_rules.token_budget_policy(),
      duplicate_prompt_rules.prompt_policy(),
      duplicate_prompt_rules.capability_cache_policy(),
      duplicate_prompt_rules.degrade_policy(),
      duplicate_prompt_rules.timeout_policy(),
      duplicate_prompt_rules.execution_policy(),
      duplicate_prompt_rules.ops_policy(),
      duplicate_prompt_rules.worker_threads(),
      duplicate_prompt_rules.multi_agent_enabled(),
      invalid_memory_policy,
  };

  assert_true(!rebuilt_missing_budget.has_consistent_values(),
              "runtime policy snapshot should reject missing runtime budget dimensions");
  assert_true(!rebuilt_duplicate_prompt_rules.has_consistent_values(),
              "runtime policy snapshot should reject duplicate prompt trust sources");
  assert_true(!rebuilt_invalid_memory_policy.has_consistent_values(),
              "runtime policy snapshot should reject unsupported memory maintenance checkpoint strategies");
}

}  // namespace

int main() {
  try {
    test_runtime_policy_snapshot_exposes_read_only_policy_fields();
    test_runtime_policy_snapshot_interface_is_const_only();
    test_runtime_policy_snapshot_rejects_incomplete_budget_or_policy_domains();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
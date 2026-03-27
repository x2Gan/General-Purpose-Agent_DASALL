#include <exception>
#include <iostream>

#include "LastKnownGoodStore.h"
#include "RuntimePolicyProvider.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::profiles::RuntimePolicySnapshotRef make_snapshot_ref(
    const std::string& profile_id) {
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

  return std::make_shared<const RuntimePolicySnapshot>(
      5U,
      profile_id,
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = 4000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"default",
               ModelRoutePolicy{
                   .route = "primary",
                   .fallback_route = std::string("backup"),
                   .streaming_enabled = true,
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
          .expire_after_ms = 3000,
          .stale_read_allowed = true,
          .failure_backoff_ms = 100,
      },
      DegradePolicy{
          .fallback_chain = {"safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .tool = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .mcp = TimeoutBudget{.timeout_ms = 1200, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .workflow = TimeoutBudget{.timeout_ms = 1600, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "strict",
          .allowed_tool_domains = {"default"},
      },
      OpsPolicy{
          .log_level = "warn",
          .metrics_granularity = "minimal",
          .trace_sample_ratio = 0.25,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "manual",
      });
}

void test_last_known_good_store_persists_snapshot_in_memory() {
  using dasall::profiles::LastKnownGoodStore;
  using dasall::tests::support::assert_true;

  LastKnownGoodStore store;
  const auto save_result = store.save(make_snapshot_ref("desktop_full"));
  const auto load_result = store.load("desktop_full");

  assert_true(save_result.ok(), "last-known-good store should save consistent snapshots");
  assert_true(load_result.ok(), "last-known-good store should load previously saved snapshots");
  assert_true(load_result.snapshot->effective_profile_id() == "desktop_full",
              "loaded snapshot should preserve profile id");
}

void test_last_known_good_store_reports_unavailable_for_missing_profile() {
  using dasall::profiles::LastKnownGoodStore;
  using dasall::profiles::ProfileErrorCode;
  using dasall::tests::support::assert_true;

  LastKnownGoodStore store;
  const auto missing = store.load("missing");

  assert_true(!missing.ok(), "load should fail when profile has no stored snapshot");
  assert_true(missing.error_code.has_value(), "missing profile should expose explicit error");
  assert_true(*missing.error_code == ProfileErrorCode::LastKnownGoodUnavailable,
              "missing profile should map to last-known-good-unavailable");
}

}  // namespace

int main() {
  try {
    test_last_known_good_store_persists_snapshot_in_memory();
    test_last_known_good_store_reports_unavailable_for_missing_profile();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

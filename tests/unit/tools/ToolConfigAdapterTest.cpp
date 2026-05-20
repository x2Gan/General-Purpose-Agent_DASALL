#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "config/ToolConfigAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::BuildProfileManifest make_manifest(
    bool tools_mcp_enabled,
    bool multi_agent_enabled) {
  return dasall::profiles::BuildProfileManifest{
      .enabled_modules = {
          "runtime",
          "tools_builtin",
          tools_mcp_enabled ? "tools_mcp" : "platform_hal",
          multi_agent_enabled ? "multi_agent" : "infra_observability",
      },
      .enabled_adapters = {},
      .observability_level = "full",
      .build_tags = {"profile:test"},
      .toolchain_hint = std::string("x86_64-linux-gnu"),
  };
}

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot(
    std::string profile_id,
    std::uint64_t generation,
    std::uint32_t max_tool_calls,
    std::int64_t tool_timeout_ms,
    std::int64_t mcp_timeout_ms,
    std::int64_t workflow_timeout_ms,
    bool stale_read_allowed,
    std::string audit_level,
    std::vector<std::string> allowed_tool_domains,
    std::vector<std::string> tool_visibility_rules) {
  return dasall::profiles::RuntimePolicySnapshot{
      generation,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = max_tool_calls,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
      },
      dasall::profiles::ModelProfile{
          .stage_routes = {{
              "planner",
              dasall::profiles::ModelRoutePolicy{
                  .route = "local.small",
                  .fallback_route = std::string("builtin_only"),
                  .streaming_enabled = false,
              },
          }},
      },
      dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
      },
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = std::move(tool_visibility_rules),
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = stale_read_allowed ? 90000 : 180000,
          .stale_read_allowed = stale_read_allowed,
          .failure_backoff_ms = stale_read_allowed ? 2000 : 5000,
      },
      dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
              .timeout_ms = tool_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = mcp_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = workflow_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = std::move(audit_level),
          .allowed_tool_domains = std::move(allowed_tool_domains),
      },
      dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

void test_profile_projection_preserves_desktop_vs_edge_policy_differences() {
  const dasall::tools::config::ToolConfigAdapter adapter;
  const auto desktop_snapshot = make_snapshot(
      "desktop_full",
      1U,
      24U,
      2500,
      2000,
      5000,
      false,
      "full",
      {"builtin", "mcp", "workflow"},
      {"builtin:all", "mcp:trusted", "workflow:skill.runtime-state-snapshot"});
  const auto edge_snapshot = make_snapshot(
      "edge_minimal",
      2U,
      4U,
      1200,
      1000,
      2500,
      true,
      "minimal",
      {"builtin"},
      {"builtin:essential"});

  const auto desktop_policy = adapter.build_policy_view(
      desktop_snapshot,
      make_manifest(true, true));
  const auto edge_policy = adapter.build_policy_view(
      edge_snapshot,
      make_manifest(false, false));
  const auto desktop_timeout = adapter.build_timeout_view(
      desktop_snapshot,
      make_manifest(true, true));
  const auto edge_timeout = adapter.build_timeout_view(
      edge_snapshot,
      make_manifest(false, false));

  assert_equal(std::string("full"), desktop_policy.audit_level,
               "desktop policy should preserve the higher audit level");
  assert_equal(std::string("minimal"), edge_policy.audit_level,
               "edge policy should preserve the constrained audit level");
    assert_equal(3, static_cast<int>(desktop_policy.allowed_tool_domains.size()),
                             "desktop policy should preserve builtin, mcp and workflow allowed domains");
  assert_equal(1, static_cast<int>(edge_policy.allowed_tool_domains.size()),
               "edge policy should collapse to builtin-only allowed domains");
  assert_equal(std::string("builtin:all"), desktop_policy.tool_visibility_rules.front(),
               "desktop policy should preserve the broader visibility rule");
  assert_equal(std::string("builtin:essential"), edge_policy.tool_visibility_rules.front(),
               "edge policy should preserve the constrained visibility rule");

  assert_equal(2500, static_cast<int>(desktop_timeout.tool.timeout_ms),
               "desktop timeout view should preserve the longer builtin timeout");
  assert_equal(1200, static_cast<int>(edge_timeout.tool.timeout_ms),
               "edge timeout view should preserve the shorter builtin timeout");
  assert_true(desktop_timeout.mcp_lane_enabled,
              "desktop manifest should keep the mcp lane enabled");
  assert_true(!edge_timeout.mcp_lane_enabled,
              "edge manifest should keep the mcp lane disabled");
  assert_equal(24, static_cast<int>(desktop_timeout.max_tool_calls),
               "desktop timeout view should preserve the larger max_tool_calls budget");
  assert_equal(4, static_cast<int>(edge_timeout.max_tool_calls),
               "edge timeout view should preserve the smaller max_tool_calls budget");
  assert_true(!desktop_timeout.stale_read_allowed,
              "desktop timeout view should keep stale mcp reads disabled");
  assert_true(edge_timeout.stale_read_allowed,
              "edge timeout view should preserve stale mcp reads when the profile allows them");
}

}  // namespace

int main() {
  try {
    test_profile_projection_preserves_desktop_vs_edge_policy_differences();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
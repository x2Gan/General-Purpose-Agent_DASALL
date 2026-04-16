#include <exception>
#include <iostream>
#include <string>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "config/ToolConfigAdapter.h"
#include "policy/ToolPolicyGate.h"
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
    std::vector<std::string> visibility_rules,
    std::vector<std::string> allowed_domains) {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
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
          .tool_visibility_rules = std::move(visibility_rules),
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 30000,
          .expire_after_ms = 180000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 5000,
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
              .timeout_ms = 2500,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = 5000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = std::move(allowed_domains),
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

void test_desktop_and_edge_policy_projection_drive_different_gate_outcomes() {
  dasall::tools::config::ToolConfigAdapter adapter;
  dasall::tools::policy::ToolPolicyGate gate;
  const auto desktop_policy = adapter.build_policy_view(
      make_snapshot("desktop_full", {"builtin:all", "mcp:trusted"}, {"builtin", "mcp"}),
      make_manifest(true, true));
  const auto edge_policy = adapter.build_policy_view(
      make_snapshot("edge_minimal", {"builtin:essential"}, {"builtin"}),
      make_manifest(false, false));
  const dasall::tools::ToolAdmissionRequest mcp_request{
      .tool_name = "remote.echo",
      .required_scopes = {},
      .caller_domain = std::string("mcp"),
      .high_risk = false,
      .confirmation_present = false,
      .route_proven = true,
  };

  const auto desktop_decision = gate.evaluate(mcp_request, desktop_policy);
  const auto edge_decision = gate.evaluate(mcp_request, edge_policy);

  assert_true(desktop_decision.allowed(),
              "desktop profile should allow trusted mcp requests after projection");
  assert_true(!edge_decision.allowed(),
              "edge profile should deny the same mcp request after projection");
  assert_equal(std::string("policy.domain_denied"), edge_decision.reason_code,
               "edge profile should deny mcp via allowed-domain gating");
}

}  // namespace

int main() {
  try {
    test_desktop_and_edge_policy_projection_drive_different_gate_outcomes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
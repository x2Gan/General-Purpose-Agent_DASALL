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

[[nodiscard]] dasall::profiles::BuildProfileManifest make_manifest() {
  return dasall::profiles::BuildProfileManifest{
      .enabled_modules = {"runtime", "tools_builtin", "tools_mcp", "multi_agent"},
      .enabled_adapters = {},
      .observability_level = "full",
      .build_tags = {"profile:test"},
      .toolchain_hint = std::string("x86_64-linux-gnu"),
  };
}

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot(
    std::uint64_t generation,
    std::int64_t tool_timeout_ms,
    std::uint32_t max_tool_calls) {
  return dasall::profiles::RuntimePolicySnapshot{
      generation,
      std::string("desktop_full"),
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
          .tool_visibility_rules = {"builtin:all"},
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
              .timeout_ms = tool_timeout_ms,
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
          .allowed_tool_domains = {"builtin", "mcp"},
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

void test_generation_fingerprint_invalidates_cached_projection_on_hot_update() {
  dasall::tools::config::ToolConfigAdapter adapter;
  const auto manifest = make_manifest();
  const auto snapshot_v1 = make_snapshot(11U, 2500, 24U);
  const auto snapshot_v2 = make_snapshot(12U, 1200, 8U);

  const auto view_v1 = adapter.build_timeout_view(snapshot_v1, manifest);
  const auto fingerprint_v1 = adapter.snapshot_fingerprint(snapshot_v1);

  assert_true(adapter.is_snapshot_current(fingerprint_v1, snapshot_v1),
              "the current snapshot fingerprint should match the snapshot that produced the cached view");
  assert_true(!adapter.is_snapshot_current(fingerprint_v1, snapshot_v2),
              "a newer generation should invalidate the previous snapshot fingerprint");

  const auto view_v2 = adapter.build_timeout_view(snapshot_v2, manifest);
  assert_equal(2500, static_cast<int>(view_v1.tool.timeout_ms),
               "the first projected timeout view should preserve the original timeout");
  assert_equal(1200, static_cast<int>(view_v2.tool.timeout_ms),
               "hot update should rebuild the timeout view from the new snapshot generation");
  assert_equal(24, static_cast<int>(view_v1.max_tool_calls),
               "the first projected timeout view should preserve the original max_tool_calls budget");
  assert_equal(8, static_cast<int>(view_v2.max_tool_calls),
               "hot update should rebuild the projected max_tool_calls budget");
}

}  // namespace

int main() {
  try {
    test_generation_fingerprint_invalidates_cached_projection_on_hot_update();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
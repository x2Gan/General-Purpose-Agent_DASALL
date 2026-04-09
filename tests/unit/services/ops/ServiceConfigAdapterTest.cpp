#include <exception>
#include <iostream>
#include <string>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "ops/ServiceConfigAdapter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::profiles::BuildProfileManifest;
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
using dasall::services::internal::AdapterRouteKind;
using dasall::services::internal::ServiceConfigAdapter;
using dasall::services::internal::ServiceQueueOverflowPolicy;

[[nodiscard]] RuntimePolicySnapshot make_runtime_policy(std::string profile_id,
                                                        std::uint32_t worker_threads,
                                                        bool stale_read_allowed,
                                                        std::string audit_level,
                                                        std::string metrics_granularity,
                                                        double trace_sample_ratio) {
  dasall::contracts::RuntimeBudget runtime_budget;
  runtime_budget.max_tokens = 4096U;
  runtime_budget.max_turns = 8U;
  runtime_budget.max_tool_calls = 8U;
  runtime_budget.max_latency_ms = 8000U;
  runtime_budget.max_replan_count = 2U;

  return RuntimePolicySnapshot{
      7U,
      std::move(profile_id),
      runtime_budget,
      ModelProfile{
          .stage_routes = {
              {"planner",
               ModelRoutePolicy{
                   .route = "lan.general",
                   .fallback_route = std::string("cloud.reasoning"),
                   .streaming_enabled = false,
               }},
              {"responder",
               ModelRoutePolicy{
                   .route = "local.small",
                   .fallback_route = std::string("lan.general"),
                   .streaming_enabled = true,
               }},
          },
      },
      TokenBudgetPolicy{
          .max_input_tokens = 4096U,
          .max_output_tokens = 1024U,
          .max_history_turns = 8U,
          .compression_threshold = 2048U,
      },
      PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all"},
      },
      CapabilityCachePolicy{
          .refresh_interval_ms = 15000,
          .expire_after_ms = 120000,
          .stale_read_allowed = stale_read_allowed,
          .failure_backoff_ms = 3000,
      },
      DegradePolicy{
          .fallback_chain = {"local.small", "builtin_only"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 2500, .retry_budget = 1U, .circuit_breaker_threshold = 4U},
          .tool = TimeoutBudget{.timeout_ms = 1800, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
          .mcp = TimeoutBudget{.timeout_ms = 1500, .retry_budget = 0U, .circuit_breaker_threshold = 2U},
          .workflow = TimeoutBudget{.timeout_ms = 4000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = std::move(audit_level),
          .allowed_tool_domains = {"builtin", "mcp"},
      },
      OpsPolicy{
          .log_level = "info",
          .metrics_granularity = std::move(metrics_granularity),
          .trace_sample_ratio = trace_sample_ratio,
          .remote_diagnostics_enabled = true,
          .upgrade_strategy = "canary",
      },
      worker_threads,
  };
}

[[nodiscard]] BuildProfileManifest make_manifest(bool platform_hal_enabled,
                                                 bool observability_enabled,
                                                 std::string observability_level) {
  BuildProfileManifest manifest{
      .enabled_modules = {"runtime", "tools_builtin"},
      .enabled_adapters = {},
      .observability_level = std::move(observability_level),
      .build_tags = {"profile:edge_balanced", "platform:linux-arm64-embedded"},
      .toolchain_hint = std::string("arm64-linux-gnu"),
  };

  if (platform_hal_enabled) {
    manifest.enabled_modules.push_back("platform_hal");
  }

  if (observability_enabled) {
    manifest.enabled_modules.push_back("infra_observability");
  }

  return manifest;
}

void test_service_config_adapter_derives_worker_timeout_overflow_and_stale_read_policy() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ServiceConfigAdapter adapter;
  const auto result = adapter.derive_policy_view(
      make_runtime_policy("desktop_full", 12U, false, "full", "full", 0.2),
      make_manifest(false, true, "full"));

  assert_true(result.ok(), "service config adapter should derive a policy view from valid inputs");
  const auto& policy = *result.policy_view;
  assert_equal(std::string("desktop_full"),
               policy.effective_profile_id,
               "derived policy should carry the effective profile id from the runtime snapshot");
  assert_equal(4,
               static_cast<int>(policy.command_lane_workers),
               "command lane workers should use the frozen min(4, floor(worker_threads / 3)) formula");
  assert_equal(3,
               static_cast<int>(policy.execution_query_lane_workers),
               "execution query workers should use the frozen floor(worker_threads / 4) formula");
  assert_equal(3,
               static_cast<int>(policy.data_query_lane_workers),
               "data query workers should use the same quarter-split formula");
  assert_equal(8000,
               static_cast<int>(policy.request_deadline_ceiling_ms),
               "request deadline ceiling should derive from runtime_budget.max_latency_ms");
  assert_equal(1800,
               static_cast<int>(policy.adapter_call_timeout_ms),
               "adapter timeout should derive from timeout_policy.tool.timeout_ms");
  assert_true(policy.command_queue_overflow_policy == ServiceQueueOverflowPolicy::reject &&
                  policy.subscription_queue_overflow_policy ==
                      ServiceQueueOverflowPolicy::drop_oldest,
              "overflow policies should stay fixed to reject/drop_oldest under the current services design");
  assert_true(!policy.default_allow_stale_reads,
              "strict profiles should derive default_allow_stale_reads=false");
  assert_true(!policy.local_platform_route_enabled,
              "local platform routing should stay disabled when the build manifest omits platform_hal");
  assert_true(policy.observability_bridge_enabled,
              "full observability manifests should enable services observability bridges");
}

void test_service_config_adapter_tracks_platform_and_stale_profile_switches() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ServiceConfigAdapter adapter;
  const auto result = adapter.derive_policy_view(
      make_runtime_policy("edge_balanced", 6U, true, "standard", "partial", 0.08),
      make_manifest(true, true, "full"));

  assert_true(result.ok(), "edge_balanced inputs should still derive a valid services policy view");
  const auto& policy = *result.policy_view;
  assert_equal(2,
               static_cast<int>(policy.command_lane_workers),
               "command worker derivation should round down from the worker thread budget");
  assert_equal(1,
               static_cast<int>(policy.execution_query_lane_workers),
               "query workers should retain at least one worker under smaller profiles");
  assert_true(policy.default_allow_stale_reads,
              "stale_read_allowed=true should become the services default read freshness baseline");
  assert_true(policy.local_platform_route_enabled,
              "platform_hal-enabled manifests should keep local platform routing available");
  assert_true(policy.adapter_preference_order.size() == 3U &&
                  policy.adapter_preference_order.front() == AdapterRouteKind::local_platform,
              "platform-enabled manifests should prefer local_platform before service and remote routes");
  assert_equal(std::string("partial"),
               policy.metrics_granularity,
               "metrics granularity should pass through from ops_policy");
}

void test_service_config_adapter_rejects_invalid_build_or_runtime_inputs() {
  using dasall::tests::support::assert_true;

  ServiceConfigAdapter adapter;

  auto invalid_manifest = make_manifest(false, true, "full");
  invalid_manifest.observability_level.clear();

  const auto invalid_manifest_result = adapter.derive_policy_view(
      make_runtime_policy("desktop_full", 12U, false, "full", "full", 0.2),
      invalid_manifest);
  assert_true(!invalid_manifest_result.ok() &&
                  invalid_manifest_result.error == "build profile manifest is inconsistent",
              "service config adapter should fail closed when the build manifest violates the frozen manifest invariants");

  const auto invalid_runtime_result = adapter.derive_policy_view(
      make_runtime_policy("desktop_full", 12U, false, "verbose", "full", 0.2),
      make_manifest(false, true, "full"));
  assert_true(!invalid_runtime_result.ok() &&
                  invalid_runtime_result.error.find("audit_level") != std::string::npos,
              "service config adapter should reject runtime policies whose services-facing audit level falls outside the supported set");
}

}  // namespace

int main() {
  try {
    test_service_config_adapter_derives_worker_timeout_overflow_and_stale_read_policy();
    test_service_config_adapter_tracks_platform_and_stale_profile_switches();
    test_service_config_adapter_rejects_invalid_build_or_runtime_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
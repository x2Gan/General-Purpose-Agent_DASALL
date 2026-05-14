#include <exception>
#include <iostream>
#include <string>

#include "AccessConfigAdapter.h"
#include "RuntimePolicySnapshot.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessBootstrapConfig;
using dasall::access::AccessConfigAdapter;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] AccessBootstrapConfig make_bootstrap_config(std::string revision) {
  AccessBootstrapConfig config;
  config.bootstrap_revision = std::move(revision);
  config.entry_type = "daemon";
  config.listen_ref = "unix:///tmp/dasall.sock";
  config.allowed_protocols = {"ipc_uds"};
  config.peer_auth_mode = "local_identity";
  config.auth_provider_ref = std::string("secret://access-auth");
  config.idempotency_window_ms = 120000;
  config.max_inflight_requests = 32;
  config.dispatch_deadline_ms = 30000;
  config.result_replay_ttl_ms = 240000;
  config.stream_heartbeat_ms = 4000;
  config.slow_consumer_max_buffer = 12;
  config.drain_timeout_ms = 12000;
  config.max_payload_bytes = 4096;
  config.max_user_input_bytes = 8192;
  config.cors_allowed_origins = {"https://console.example"};
  config.session_id_mode = "auto";
  config.trusted_local_subjects = {"local://uid/1000", "local://uid/0"};
  config.ownership_token_hmac_secret_ref = std::string("secret://access-hmac");
  return config;
}

[[nodiscard]] RuntimePolicySnapshot make_snapshot(std::uint64_t generation,
                                                  std::string profile_id,
                                                  std::uint32_t max_latency_ms,
                                                  std::int64_t workflow_timeout_ms,
                                                  std::string log_level,
                                                  double trace_sample_ratio,
                                                  bool remote_diagnostics_enabled) {
  return RuntimePolicySnapshot{
      generation,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 12U,
          .max_latency_ms = max_latency_ms,
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
              .timeout_ms = 2200,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = workflow_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin", "mcp"},
      },
      dasall::profiles::OpsPolicy{
          .log_level = std::move(log_level),
          .metrics_granularity = "full",
          .trace_sample_ratio = trace_sample_ratio,
          .remote_diagnostics_enabled = remote_diagnostics_enabled,
          .upgrade_strategy = "rolling",
      },
      4U};
}

void access_config_adapter_projects_runtime_tightened_views_and_invalidates_on_hot_update() {
  AccessConfigAdapter adapter;
  const auto bootstrap = make_bootstrap_config("bootstrap-rev-1");
  const auto snapshot_v1 = make_snapshot(11U, "desktop_full", 8000U, 5000, "warn", 0.25, true);
  const auto snapshot_v2 = make_snapshot(12U, "desktop_full", 1800U, 900, "error", 0.1, false);

  const auto projected_v1 = adapter.project(bootstrap, snapshot_v1);
  assert_true(projected_v1.ok() && projected_v1.projection.has_value(),
              "projection should succeed for a consistent bootstrap and runtime snapshot");
  assert_equal(std::string("bootstrap-rev-1"),
               projected_v1.projection->fingerprint.bootstrap_revision,
               "projection should preserve the bootstrap revision in the fingerprint");
  assert_equal(11, static_cast<int>(projected_v1.projection->fingerprint.runtime_policy_generation),
               "projection should preserve the runtime policy generation in the fingerprint");
  assert_true(adapter.is_snapshot_current(projected_v1.projection->fingerprint,
                                          bootstrap,
                                          snapshot_v1),
              "projection fingerprint should be current for the snapshot that produced it");
  assert_equal(std::string("local_identity"), projected_v1.projection->auth_view.peer_auth_mode,
               "projection should preserve peer_auth_mode from bootstrap config");
  assert_true(projected_v1.projection->auth_view.strict_auth_required,
              "safe-mode runtime policy should keep auth strict even for local_identity bootstrap mode");
  assert_equal(5000, projected_v1.projection->admission_view.dispatch_deadline_ms,
               "projection should tighten dispatch deadline to the runtime workflow timeout");
  assert_true(projected_v1.projection->admission_view.default_deny,
              "projection should be deny-oriented when runtime execution policy keeps safe mode enabled");
  assert_equal(5000, projected_v1.projection->publish_view.drain_timeout_ms,
               "projection should tighten publish drain timeout to the runtime workflow timeout");
  assert_equal(4096, projected_v1.projection->publish_view.max_user_input_bytes,
               "projection should clamp max_user_input_bytes to the payload ceiling");
  assert_equal(std::string("warn"),
               projected_v1.projection->runtime_governance_view.ops_log_level,
               "projection should surface the runtime ops log level");
  assert_equal(std::string("0.25"),
               projected_v1.projection->runtime_governance_view.ops_trace_sample_ratio,
               "projection should format trace_sample_ratio as a stable string");
  assert_true(projected_v1.projection->runtime_governance_view.remote_diagnostics_enabled,
              "projection should surface remote diagnostics policy");
  assert_true(projected_v1.projection->runtime_governance_view.runtime_budget_profile.find("tokens:4096") !=
                  std::string::npos,
              "projection should encode runtime budget facts into the governance view");
  assert_true(projected_v1.projection->runtime_governance_view.timeout_policy_profile.find("workflow:5000") !=
                  std::string::npos,
              "projection should encode timeout policy facts into the governance view");

  const auto projected_v2 = adapter.project(bootstrap, snapshot_v2);
  assert_true(projected_v2.ok() && projected_v2.projection.has_value(),
              "projection should rebuild after a newer runtime policy snapshot arrives");
  assert_true(!adapter.is_snapshot_current(projected_v1.projection->fingerprint,
                                           bootstrap,
                                           snapshot_v2),
              "newer runtime policy generation should invalidate the previous fingerprint");
  assert_equal(900, projected_v2.projection->admission_view.dispatch_deadline_ms,
               "hot update should rebuild the tightened dispatch deadline from the new snapshot");
  assert_equal(std::string("error"),
               projected_v2.projection->runtime_governance_view.ops_log_level,
               "hot update should rebuild runtime governance fields from the new snapshot");
  assert_true(!projected_v2.projection->runtime_governance_view.remote_diagnostics_enabled,
              "hot update should rebuild remote diagnostics policy from the new snapshot");

  const auto last_known_good = adapter.last_known_good_projection();
  assert_true(last_known_good.has_value(),
              "successful projection should refresh the last-known-good cached projection");
  assert_equal(12, static_cast<int>(last_known_good->fingerprint.runtime_policy_generation),
               "last-known-good should track the latest successful projection");
}

}  // namespace

int main() {
  try {
    access_config_adapter_projects_runtime_tightened_views_and_invalidates_on_hot_update();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
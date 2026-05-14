#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AccessGatewayFactory.h"
#include "AccessConfigAdapter.h"
#include "RuntimePolicySnapshot.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessBootstrapConfig;
using dasall::access::AccessConfigAdapter;
using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] AccessBootstrapConfig make_bootstrap_config() {
  AccessBootstrapConfig config;
  config.bootstrap_revision = "bootstrap-rev-good";
  config.entry_type = "gateway";
  config.listen_ref = "http://127.0.0.1:8080";
  config.allowed_protocols = {"http_unary"};
  config.peer_auth_mode = "strict";
  config.idempotency_window_ms = 120000;
  config.max_inflight_requests = 32;
  config.dispatch_deadline_ms = 30000;
  config.result_replay_ttl_ms = 240000;
  config.stream_heartbeat_ms = 4000;
  config.slow_consumer_max_buffer = 12;
  config.drain_timeout_ms = 12000;
  config.max_payload_bytes = 4096;
  config.max_user_input_bytes = 2048;
  config.cors_allowed_origins = {"https://console.example"};
  config.session_id_mode = "auto";
  return config;
}

[[nodiscard]] RuntimePolicySnapshot make_snapshot(std::uint64_t generation,
                                                  std::string log_level) {
  return RuntimePolicySnapshot{
      generation,
      std::string("desktop_full"),
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 12U,
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
              .timeout_ms = 5000,
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
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

[[nodiscard]] bool daemon_gateway_init_ok(const AccessBootstrapConfig& bootstrap_config,
                                                                                    const RuntimePolicySnapshot& snapshot) {
    DaemonAccessPipelineOptions options;
    options.bootstrap_config = bootstrap_config;
    options.bootstrap_config.entry_type = "daemon";
    options.bootstrap_config.listen_ref = "unix:///tmp/dasall-invalid-schema.sock";
    options.bootstrap_config.allowed_protocols = {"ipc_uds"};
    options.bootstrap_config.trusted_local_subjects = {"local://uid/1000"};
    options.derive_views_from_runtime_policy = true;
    options.runtime_policy_snapshot =
            std::make_shared<RuntimePolicySnapshot>(snapshot);
    options.runtime_dispatch_backend = [](const RuntimeDispatchRequest&) {
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
    };

    auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
    assert_true(gateway != nullptr,
                            "invalid schema test should still create a concrete daemon gateway object");
    return gateway->init();
}

void access_config_adapter_rejects_invalid_schema_without_polluting_last_known_good() {
  AccessConfigAdapter adapter;
  const auto good_bootstrap = make_bootstrap_config();
  const auto good_snapshot = make_snapshot(11U, "info");

  const auto initial_projection = adapter.project(good_bootstrap, good_snapshot);
  assert_true(initial_projection.ok() && initial_projection.projection.has_value(),
              "a consistent baseline projection should seed the last-known-good cache");

  auto invalid_bootstrap = good_bootstrap;
  invalid_bootstrap.bootstrap_revision.clear();
  const auto bootstrap_failure = adapter.project(invalid_bootstrap, good_snapshot);
  assert_true(!bootstrap_failure.ok(),
              "missing bootstrap revision should reject invalid bootstrap schema");
  assert_equal(std::string("access bootstrap config is inconsistent"), bootstrap_failure.error,
               "invalid bootstrap should fail with a concrete adapter error");

  const auto last_known_good_after_bootstrap_failure = adapter.last_known_good_projection();
  assert_true(last_known_good_after_bootstrap_failure.has_value(),
              "invalid bootstrap should preserve the previous last-known-good projection");
  assert_equal(std::string("bootstrap-rev-good"),
               last_known_good_after_bootstrap_failure->fingerprint.bootstrap_revision,
               "invalid bootstrap must not replace the last-known-good projection");

  const auto invalid_snapshot = make_snapshot(0U, "");
  const auto snapshot_failure = adapter.project(good_bootstrap, invalid_snapshot);
  assert_true(!snapshot_failure.ok(),
              "runtime snapshot with invalid schema should be rejected");
  assert_equal(std::string("runtime policy snapshot is inconsistent"), snapshot_failure.error,
               "invalid runtime snapshot should fail with a concrete adapter error");

  const auto last_known_good_after_snapshot_failure = adapter.last_known_good_projection();
  assert_true(last_known_good_after_snapshot_failure.has_value(),
              "invalid runtime snapshot should preserve the previous last-known-good projection");
  assert_equal(11,
               static_cast<int>(last_known_good_after_snapshot_failure->fingerprint.runtime_policy_generation),
               "invalid runtime snapshot must not replace the last-known-good generation");
  assert_equal(std::string("info"),
               last_known_good_after_snapshot_failure->runtime_governance_view.ops_log_level,
               "invalid runtime snapshot must leave the last-known-good governance view intact");

    assert_true(!daemon_gateway_init_ok(invalid_bootstrap, good_snapshot),
                            "invalid bootstrap schema must fail closed during daemon gateway init");
    assert_true(!daemon_gateway_init_ok(good_bootstrap, invalid_snapshot),
                            "invalid runtime snapshot schema must fail closed during daemon gateway init");
}

}  // namespace

int main() {
  try {
    access_config_adapter_rejects_invalid_schema_without_polluting_last_known_good();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
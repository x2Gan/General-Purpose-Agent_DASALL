#include <exception>
#include <iostream>
#include <string>

#include "RuntimePolicySnapshot.h"
#include "ServiceLiveComposition.h"
#include "health/IHealthProbe.h"
#include "health/ProbeTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ProbeStatus;
using dasall::services::DataQueryRequest;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ServiceCallContext;
using dasall::services::ServiceDataFreshness;
using dasall::tests::support::assert_true;

constexpr char kExecutionCapabilityId[] = "agent.terminal";
constexpr char kDataCapabilityId[] = "agent.dataset";

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  return dasall::profiles::RuntimePolicySnapshot{
      1U,
      "desktop_full",
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
          .tool_visibility_rules = {"builtin:all"},
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
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
          .allowed_tool_domains = {"builtin"},
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

[[nodiscard]] ServiceCallContext make_context(const std::string& request_id) {
  return ServiceCallContext{
      .request_id = request_id,
      .session_id = "session-services-production-adapter",
      .trace_id = "trace-services-production-adapter",
      .tool_call_id = "tool-services-production-adapter",
      .goal_id = "goal-services-production-adapter",
      .budget_guard = std::nullopt,
      .deadline_ms = 1712746905000ULL,
  };
}

[[nodiscard]] ExecutionCommandRequest make_action_request(const std::string& request_id) {
  return ExecutionCommandRequest{
      .context = make_context(request_id),
      .target = {
          .capability_id = kExecutionCapabilityId,
          .target_id = "target-services-production-adapter",
      },
      .action = kExecutionCapabilityId,
      .arguments_json = "{\"command\":\"echo production-adapter\"}",
      .idempotency_key = std::nullopt,
  };
}

[[nodiscard]] DataQueryRequest make_query_request(const std::string& request_id) {
  return DataQueryRequest{
      .context = make_context(request_id),
      .dataset = kDataCapabilityId,
      .filters_json = "{}",
      .projection = "default",
      .freshness = ServiceDataFreshness::strict,
  };
}

[[nodiscard]] dasall::services::ServiceLiveBackendResult make_backend_result(
    const std::string& route_name,
    const dasall::services::ServiceLiveBackendRequest& request,
    const std::string& provider_status_code,
    std::uint32_t latency_ms) {
  if (request.request_kind == dasall::services::ServiceLiveRequestKind::action) {
    return dasall::services::ServiceLiveBackendResult{
        .transport_outcome =
            dasall::services::ServiceLiveTransportOutcome::acknowledged,
        .provider_status_code = provider_status_code,
        .payload_json = std::string("{\"route\":\"") + route_name +
                        "\",\"operation\":\"" + request.operation_name + "\"}",
        .latency_ms = latency_ms,
        .side_effects = {request.operation_name + "." + route_name + ".applied"},
        .evidence_refs = {std::string("test://") + route_name + "/action/" +
                          request.operation_name},
    };
  }

  if (request.operation_name == "catalog.list") {
    return dasall::services::ServiceLiveBackendResult{
        .transport_outcome =
            dasall::services::ServiceLiveTransportOutcome::acknowledged,
        .provider_status_code = provider_status_code,
        .payload_json = std::string("{\"target_class\":\"") +
                        request.capability_id + "\",\"route\":\"" + route_name +
                        "\"}",
        .latency_ms = latency_ms,
        .side_effects = {},
        .evidence_refs = {std::string("test://") + route_name + "/catalog/" +
                          request.capability_id},
    };
  }

  return dasall::services::ServiceLiveBackendResult{
      .transport_outcome = dasall::services::ServiceLiveTransportOutcome::acknowledged,
      .provider_status_code = provider_status_code,
      .payload_json = std::string("[{\"capability_id\":\"") +
                      request.capability_id + "\",\"projection\":\"" +
                      request.operation_name + "\",\"route\":\"" + route_name +
                      "\"}]",
      .latency_ms = latency_ms,
      .side_effects = {},
      .evidence_refs = {std::string("test://") + route_name + "/query/" +
                        request.operation_name},
  };
}

[[nodiscard]] dasall::services::ServiceLiveAdapterRegistry make_registry(
    dasall::services::ServiceLiveAvailabilityState local_platform_state,
    dasall::services::ServiceLiveAvailabilityState local_service_state,
    dasall::services::ServiceLiveAvailabilityState remote_service_state,
    bool remote_timeout) {
  return dasall::services::ServiceLiveAdapterRegistry{
      .route_equivalence_class = "service.live",
      .local_platform = dasall::services::ServiceLiveRouteBinding{
          .adapter_id = "test.local_platform",
          .trust_class = dasall::services::ServiceLiveTrustClass::trusted_local,
          .availability_state = local_platform_state,
          .supported_capabilities = {kExecutionCapabilityId},
          .handler = [](const dasall::services::ServiceLiveBackendRequest& request) {
            return make_backend_result("local_platform", request, "ok", 2U);
          },
          .timeout_on_invoke = false,
      },
      .local_service = dasall::services::ServiceLiveRouteBinding{
          .adapter_id = "test.local_service",
          .trust_class = dasall::services::ServiceLiveTrustClass::caller_verified,
          .availability_state = local_service_state,
          .supported_capabilities = {kExecutionCapabilityId, kDataCapabilityId},
          .handler = [](const dasall::services::ServiceLiveBackendRequest& request) {
            return make_backend_result("local_service", request, "ok", 5U);
          },
          .timeout_on_invoke = false,
      },
      .remote_service = dasall::services::ServiceLiveRouteBinding{
          .adapter_id = "test.remote_service",
          .trust_class = dasall::services::ServiceLiveTrustClass::caller_verified,
          .availability_state = remote_service_state,
          .supported_capabilities = {kExecutionCapabilityId, kDataCapabilityId},
          .handler = [](const dasall::services::ServiceLiveBackendRequest& request) {
            return make_backend_result("remote_service", request, "accepted", 8U);
          },
          .timeout_on_invoke = remote_timeout,
      },
  };
}

[[nodiscard]] dasall::services::ServiceLiveCompositionResult compose_with_registry(
    bool local_platform_route_enabled,
    dasall::services::ServiceLiveAvailabilityState local_platform_state,
    dasall::services::ServiceLiveAvailabilityState local_service_state,
    dasall::services::ServiceLiveAvailabilityState remote_service_state,
    bool remote_timeout,
    bool allow_route_degrade) {
  auto snapshot = make_snapshot();
  dasall::services::ServiceLiveCompositionOptions options;
  options.execution_capability_id = kExecutionCapabilityId;
  options.data_capability_id = kDataCapabilityId;
  options.local_service_available =
      local_service_state == dasall::services::ServiceLiveAvailabilityState::available ||
      local_service_state == dasall::services::ServiceLiveAvailabilityState::degraded;
  options.remote_service_available =
      remote_service_state == dasall::services::ServiceLiveAvailabilityState::available ||
      remote_service_state == dasall::services::ServiceLiveAvailabilityState::degraded;
  options.remote_timeout = remote_timeout;
  options.allow_route_degrade = allow_route_degrade;
  options.local_platform_route_enabled = local_platform_route_enabled;
  options.health_probe_enabled = true;
  options.high_risk_actions = {};
  options.adapter_registry = make_registry(local_platform_state,
                                           local_service_state,
                                           remote_service_state,
                                           remote_timeout);
  return dasall::services::compose_live_services(snapshot, options);
}

void test_production_adapter_registry_prefers_local_platform_when_profile_enables_it() {
  const auto live_services = compose_with_registry(
      true,
      dasall::services::ServiceLiveAvailabilityState::available,
      dasall::services::ServiceLiveAvailabilityState::available,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      false,
      true);
  assert_true(live_services.ok(),
              std::string("production adapter integration should compose local-platform route: ") +
                  live_services.error);
  assert_true(live_services.health_probe != nullptr,
              "production adapter integration should compose a services health probe");

  const auto result =
      live_services.execution_service->execute(make_action_request("req-prod-adapter-platform"));
  assert_true(result.succeeded() &&
                  result.payload_json.find("\"route\":\"local_platform\"") != std::string::npos,
              "production adapter integration should prefer local_platform when the profile enables the route");

  const auto probe_result = live_services.health_probe->probe();
  assert_true(probe_result.status == ProbeStatus::Healthy,
              "production adapter integration should keep the services health probe healthy when a preferred platform route is available");
}

void test_production_adapter_registry_skips_platform_when_profile_disables_it() {
  const auto live_services = compose_with_registry(
      false,
      dasall::services::ServiceLiveAvailabilityState::available,
      dasall::services::ServiceLiveAvailabilityState::available,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      false,
      true);
  assert_true(live_services.ok(),
              std::string("production adapter integration should compose local-service fallback: ") +
                  live_services.error);

  const auto result = live_services.execution_service->execute(
      make_action_request("req-prod-adapter-local-service"));
  assert_true(result.succeeded() &&
                  result.payload_json.find("\"route\":\"local_service\"") != std::string::npos,
              "production adapter integration should skip local_platform when the derived profile disables that route");
}

void test_production_adapter_registry_falls_back_to_remote_when_local_service_is_unavailable() {
  const auto live_services = compose_with_registry(
      false,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      dasall::services::ServiceLiveAvailabilityState::available,
      false,
      true);
  assert_true(live_services.ok(),
              std::string("production adapter integration should compose remote fallback: ") +
                  live_services.error);

  const auto result =
      live_services.data_service->query(make_query_request("req-prod-adapter-remote-fallback"));
  assert_true(result.succeeded() &&
                  result.rows_json.find("\"route\":\"remote_service\"") != std::string::npos,
              "production adapter integration should route through the remote handler when local_service is unavailable");
}

void test_production_adapter_registry_surfaces_remote_timeout() {
  const auto live_services = compose_with_registry(
      false,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      dasall::services::ServiceLiveAvailabilityState::available,
      true,
      true);
  assert_true(live_services.ok(),
              std::string("production adapter integration should compose remote timeout path: ") +
                  live_services.error);

  const auto result =
      live_services.data_service->query(make_query_request("req-prod-adapter-remote-timeout"));
  assert_true(!result.succeeded() && result.code.has_value() &&
                  *result.code == dasall::contracts::ResultCode::ProviderTimeout &&
                  result.error.has_value() &&
                  result.error->details.stage == "data_query_lane",
              "production adapter integration should surface remote timeout as ProviderTimeout on the data lane");
}

void test_production_adapter_registry_blocks_fallback_when_degrade_is_forbidden() {
  const auto live_services = compose_with_registry(
      false,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      dasall::services::ServiceLiveAvailabilityState::unavailable,
      dasall::services::ServiceLiveAvailabilityState::available,
      false,
      false);
  assert_true(live_services.ok(),
              std::string("production adapter integration should compose fallback-blocked path: ") +
                  live_services.error);

  const auto result = live_services.data_service->query(
      make_query_request("req-prod-adapter-fallback-blocked"));
  assert_true(!result.succeeded() && result.code.has_value() &&
                  *result.code == dasall::contracts::ResultCode::RuntimeRetryExhausted &&
                  result.error.has_value() &&
                  result.error->details.stage == "adapter_router" &&
                  result.error->details.message == "fallback_blocked",
              "production adapter integration should fail closed when fallback is forbidden by the runtime envelope");
}

}  // namespace

int main() {
  try {
    test_production_adapter_registry_prefers_local_platform_when_profile_enables_it();
    test_production_adapter_registry_skips_platform_when_profile_disables_it();
    test_production_adapter_registry_falls_back_to_remote_when_local_service_is_unavailable();
    test_production_adapter_registry_surfaces_remote_timeout();
    test_production_adapter_registry_blocks_fallback_when_degrade_is_forbidden();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
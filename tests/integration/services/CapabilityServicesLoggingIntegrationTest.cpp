#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ServiceLiveComposition.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::InfraContext;
using dasall::infra::logging::ILogDispatchBackend;
using dasall::infra::logging::LogEvent;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LogWriteResult;
using dasall::infra::logging::LoggingFacade;
using dasall::services::DataCatalogRequest;
using dasall::services::DataQueryRequest;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ServiceCallContext;
using dasall::services::ServiceDataFreshness;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr char kExecutionCapabilityId[] = "agent.terminal";
constexpr char kDataCapabilityId[] = "agent.dataset";

class RecordingDispatchBackend final : public ILogDispatchBackend {
 public:
  LogWriteResult dispatch(const LogEvent& event) override {
    events.push_back(event);
    return LogWriteResult::success();
  }

  LogWriteResult flush(const LogFlushDeadline&) override {
    return LogWriteResult::success();
  }

  std::vector<LogEvent> events;
};

[[nodiscard]] std::shared_ptr<LoggingFacade> make_logger(RecordingDispatchBackend** backend_out) {
  auto backend = std::make_unique<RecordingDispatchBackend>();
  *backend_out = backend.get();
  auto logger = std::make_shared<LoggingFacade>(std::move(backend));
  assert_true(logger->init(InfraContext{
                          .request_id = std::string("req-services-live-logging"),
                          .session_id = std::string("session-services-live-logging"),
                          .trace_id = std::string("trace-services-live-logging"),
                          .task_id = std::string("task-services-live-logging"),
                          .parent_task_id = std::string("parent-services-live-logging"),
                          .lease_id = std::string("lease-services-live-logging"),
                      })
                      .ok,
              "services logging integration should initialize the shared logger before composing live services");
  return logger;
}

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
      .session_id = "session-services-live-logging",
      .trace_id = "trace-services-live-logging",
      .tool_call_id = "tool-services-live-logging",
      .goal_id = "goal-services-live-logging",
      .budget_guard = std::nullopt,
      .deadline_ms = 1712746905000ULL,
  };
}

[[nodiscard]] ExecutionCommandRequest make_action_request(const std::string& request_id) {
  return ExecutionCommandRequest{
      .context = make_context(request_id),
      .target = {
          .capability_id = kExecutionCapabilityId,
          .target_id = "target-services-live-logging",
      },
      .action = kExecutionCapabilityId,
      .arguments_json = "{\"command\":\"echo services-live-logging\"}",
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

[[nodiscard]] DataCatalogRequest make_catalog_request(const std::string& request_id) {
  return DataCatalogRequest{
      .context = make_context(request_id),
      .target_class = kDataCapabilityId,
  };
}

[[nodiscard]] dasall::services::ServiceLiveBackendResult make_backend_result(
    const std::string& route_name,
    const dasall::services::ServiceLiveBackendRequest& request,
    const std::string& provider_status_code,
    const std::uint32_t latency_ms) {
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

[[nodiscard]] bool has_attr(const LogEvent::AttributeMap& attrs,
                            const std::string& key,
                            const std::string& value) {
  const auto it = attrs.find(key);
  return it != attrs.end() && it->second == value;
}

void test_capability_services_logging_integration_uses_live_logger_sink() {
  RecordingDispatchBackend* backend = nullptr;
  const auto logger = make_logger(&backend);
  auto snapshot = make_snapshot();
  dasall::services::ServiceLiveCompositionOptions options;
  options.execution_capability_id = kExecutionCapabilityId;
  options.data_capability_id = kDataCapabilityId;
  options.local_service_available = true;
  options.remote_service_available = false;
  options.allow_route_degrade = true;
  options.local_platform_route_enabled = false;
  options.high_risk_actions = {};
  options.logger = logger;
  options.adapter_registry = dasall::services::ServiceLiveAdapterRegistry{
      .route_equivalence_class = "service.live",
      .local_platform = std::nullopt,
      .local_service = dasall::services::ServiceLiveRouteBinding{
          .adapter_id = "test.local_service",
          .trust_class = dasall::services::ServiceLiveTrustClass::caller_verified,
          .availability_state = dasall::services::ServiceLiveAvailabilityState::available,
          .supported_capabilities = {kExecutionCapabilityId, kDataCapabilityId},
          .handler = [](const dasall::services::ServiceLiveBackendRequest& request) {
            return make_backend_result("local_service", request, "ok", 5U);
          },
          .timeout_on_invoke = false,
      },
      .remote_service = std::nullopt,
  };

  const auto live_services = dasall::services::compose_live_services(snapshot, options);
  assert_true(live_services.ok(),
              std::string("services logging integration should compose live services with a logger: ") +
                  live_services.error);

  const auto execute_result = live_services.execution_service->execute(
      make_action_request("req-services-logging-exec"));
  const auto query_result = live_services.data_service->query(
      make_query_request("req-services-logging-query"));
  const auto catalog_result = live_services.data_service->list_capabilities(
      make_catalog_request("req-services-logging-catalog"));

  assert_true(execute_result.succeeded() && query_result.succeeded() && catalog_result.succeeded(),
              "services logging integration should keep execute/query/catalog requests successful while logging through the shared sink");
  assert_true(logger->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "services logging integration should flush the shared logger before inspecting dispatched services records");
  assert_equal(3,
               static_cast<int>(backend->events.size()),
               "services logging integration should dispatch one structured services log record for execute, query, and catalog routes");

  const auto& execution_log = backend->events.at(0);
  const auto& query_log = backend->events.at(1);
  const auto& catalog_log = backend->events.at(2);
  assert_true(has_attr(execution_log.attrs, "event_name", "service.execution.route") &&
                  has_attr(execution_log.attrs, "request_id", "req-services-logging-exec") &&
                  has_attr(execution_log.attrs, "capability_id", kExecutionCapabilityId) &&
                  has_attr(execution_log.attrs, "target_id", "target-services-live-logging") &&
                  has_attr(execution_log.attrs, "operation_name", kExecutionCapabilityId) &&
                  has_attr(execution_log.attrs, "route_kind", "local_service") &&
                  has_attr(execution_log.attrs, "adapter_id", "test.local_service") &&
                  has_attr(execution_log.attrs, "side_effect_count", "1"),
              "services logging integration should persist execution route attrs through the live logger sink");
  assert_true(has_attr(query_log.attrs, "event_name", "service.data.query.route") &&
                  has_attr(query_log.attrs, "request_id", "req-services-logging-query") &&
                  has_attr(query_log.attrs, "capability_id", kDataCapabilityId) &&
                  has_attr(query_log.attrs, "target_id", kDataCapabilityId) &&
                  has_attr(query_log.attrs, "operation_name", "default") &&
                  has_attr(query_log.attrs, "evidence_ref_count", "1"),
              "services logging integration should persist data query route attrs through the live logger sink");
  assert_true(has_attr(catalog_log.attrs, "event_name", "service.data.catalog.route") &&
                  has_attr(catalog_log.attrs, "request_id", "req-services-logging-catalog") &&
                  has_attr(catalog_log.attrs, "operation_name", "catalog.list") &&
                  has_attr(catalog_log.attrs, "provider_status_code", "ok") &&
                  !has_attr(catalog_log.attrs, "payload_json", "true"),
              "services logging integration should persist catalog route attrs without leaking payload_json through the live logger sink");
}

}  // namespace

int main() {
  try {
    test_capability_services_logging_integration_uses_live_logger_sink();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
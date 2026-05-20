#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ServiceFacade.h"
#include "ServiceTypes.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "adapters/LocalServiceAdapter.h"
#include "adapters/RemoteServiceAdapter.h"
#include "data/DataProjectionCache.h"
#include "data/DataQueryLane.h"
#include "execution/ExecutionCommandLane.h"
#include "execution/ExecutionSubscriptionHub.h"
#include "mapping/ResultMapper.h"

namespace dasall::services::internal {
class ServiceAuditBridge;
class ServiceMetricsBridge;
class ServiceTraceBridge;
}  // namespace dasall::services::internal

namespace dasall::tests::mocks {

struct CapabilityServicesLoopbackFixtureOptions {
  std::string profile_id = "desktop_full";
  std::string execution_capability_id = "cap.exec";
  std::string data_capability_id = "devices";
  std::uint64_t now_ms = 1712746800000ULL;
  std::uint64_t cache_ttl_ms = 5000U;
  std::size_t max_buffered_subscription_events = 64U;
  bool local_service_available = true;
  bool remote_service_available = false;
  bool remote_timeout = false;
  bool allow_route_degrade = true;
  std::vector<std::string> critical_actions;
  std::vector<std::string> high_risk_actions;
  bool allow_high_risk_actions = true;
  std::function<std::vector<std::string>(
      const std::string& capability_id,
      const std::string& action,
      const std::string& capability_version,
      const services::internal::AdapterReceipt& receipt)>
      lookup_compensation_hints;
  services::internal::ServiceAuditBridge* audit_bridge = nullptr;
  services::internal::ServiceMetricsBridge* metrics_bridge = nullptr;
  services::internal::ServiceTraceBridge* trace_bridge = nullptr;
  std::function<services::internal::AdapterInvocationResult(
      const services::internal::AdapterInvocationRequest& request)>
      local_handler;
  std::function<services::internal::AdapterInvocationResult(
      const services::internal::AdapterInvocationRequest& request)>
      remote_handler;
};

// Header-only fixture that reuses production services lanes and adapters with
// injected loopback handlers, so integration tests can stay in tests/ without
// introducing production-only adapter variants.
class CapabilityServicesLoopbackFixture {
 public:
  explicit CapabilityServicesLoopbackFixture(
      CapabilityServicesLoopbackFixtureOptions options = {})
      : options_(std::move(options)) {
    local_service_adapter_ = std::make_unique<services::internal::LocalServiceAdapter>(
        services::internal::LocalServiceAdapterOptions{
            .service_endpoint_available = options_.local_service_available,
            .adapter_id = "loopback.local_service",
            .invoke_service = [this](const services::internal::AdapterInvocationRequest& request) {
              local_requests_.push_back(request);
              if (options_.local_handler) {
                return options_.local_handler(request);
              }
              return make_default_local_result(request);
            },
        });

    remote_service_adapter_ = std::make_unique<services::internal::RemoteServiceAdapter>(
        services::internal::RemoteServiceAdapterOptions{
            .remote_endpoint_available = wants_remote_route(),
            .timeout_on_invoke = options_.remote_timeout,
            .adapter_id = "loopback.remote_service",
            .invoke_remote = [this](const services::internal::AdapterInvocationRequest& request) {
              remote_requests_.push_back(request);
              if (options_.remote_handler) {
                return options_.remote_handler(request);
              }
              return make_default_remote_result(request);
            },
        });

    bridge_ = std::make_unique<services::internal::AdapterBridge>(
      services::internal::AdapterBridgeDependencies{
        .invokers = build_invokers(),
        .trace_bridge = options_.trace_bridge,
      });

    projection_cache_ = std::make_unique<services::internal::DataProjectionCache>(
        services::internal::DataProjectionCacheDependencies{
            .ttl_ms = options_.cache_ttl_ms,
            .now_ms = [this]() { return options_.now_ms; },
        });

    command_lane_ = std::make_unique<services::internal::ExecutionCommandLane>(
        services::internal::ExecutionCommandLaneDependencies{
            .router = &router_,
            .bridge = bridge_.get(),
            .result_mapper = &result_mapper_,
            .compensation_catalog = nullptr,
            .policy_view = build_policy_view(),
            .capability_snapshot = build_execution_snapshot(),
            .fallback_envelope = build_fallback_envelope("command.standard"),
            .registered_candidates = build_candidates(),
            .critical_actions = options_.critical_actions,
            .high_risk_actions = options_.high_risk_actions,
            .allow_high_risk_actions = options_.allow_high_risk_actions,
            .lookup_compensation_hints = build_compensation_lookup(),
            .make_execution_id = {},
            .make_compensation_execution_id = {},
            .on_serialization_acquired = {},
            .audit_bridge = options_.audit_bridge,
            .metrics_bridge = options_.metrics_bridge,
            .trace_bridge = options_.trace_bridge,
        });

    data_query_lane_ = std::make_unique<services::internal::DataQueryLane>(
        services::internal::DataQueryLaneDependencies{
            .router = &router_,
            .bridge = bridge_.get(),
            .result_mapper = &result_mapper_,
            .projection_cache = projection_cache_.get(),
            .policy_view = build_policy_view(),
            .capability_snapshot = build_data_snapshot(),
            .fallback_envelope = build_fallback_envelope("query.read_only"),
            .registered_candidates = build_candidates(),
            .metrics_bridge = options_.metrics_bridge,
            .trace_bridge = options_.trace_bridge,
        });

        subscription_hub_ = std::make_unique<services::internal::ExecutionSubscriptionHub>(
          services::internal::ExecutionSubscriptionHubDependencies{
            .max_buffered_events = options_.max_buffered_subscription_events,
            .metrics_bridge = options_.metrics_bridge,
            .trace_bridge = options_.trace_bridge,
          });

    facade_ = std::make_unique<services::internal::ServiceFacade>(
        services::internal::ServiceFacadeDependencies{
            .context_builder = &context_builder_,
            .execute_command = [this](const services::ServiceCallContext& context,
                                      const services::ExecutionCommandRequest& request) {
              return command_lane_->execute(context, request);
            },
            .compensate_command = {},
            .query_execution_state = {},
            .subscribe_execution_state = [this](const services::ServiceCallContext& context,
                              const services::ExecutionSubscriptionRequest& request) {
              return subscription_hub_->subscribe(context, request);
            },
            .diagnose_execution_target = {},
            .query_data = [this](const services::ServiceCallContext& context,
                                 const services::DataQueryRequest& request) {
              return data_query_lane_->query(context, request);
            },
            .list_data_capabilities = [this](const services::ServiceCallContext& context,
                                             const services::DataCatalogRequest& request) {
              return data_query_lane_->list_capabilities(context, request);
            },
            .trace_bridge = options_.trace_bridge,
        });
  }

  CapabilityServicesLoopbackFixture(const CapabilityServicesLoopbackFixture&) = delete;
  CapabilityServicesLoopbackFixture& operator=(const CapabilityServicesLoopbackFixture&) = delete;
  CapabilityServicesLoopbackFixture(CapabilityServicesLoopbackFixture&&) = delete;
  CapabilityServicesLoopbackFixture& operator=(CapabilityServicesLoopbackFixture&&) = delete;

  [[nodiscard]] services::IExecutionService& execution_service() {
    return *facade_;
  }

  [[nodiscard]] services::IDataService& data_service() {
    return *facade_;
  }

  [[nodiscard]] services::internal::ServiceFacade& facade() {
    return *facade_;
  }

  [[nodiscard]] services::ServiceCallContext make_context(
      std::string request_id = "req-loopback") const {
    contracts::RuntimeBudget budget;
    budget.max_latency_ms = 4000U;

    return services::ServiceCallContext{
        .request_id = request_id,
        .session_id = request_id + ".session",
        .trace_id = request_id + ".trace",
        .tool_call_id = request_id + ".tool",
        .goal_id = request_id + ".goal",
        .budget_guard = budget,
        .deadline_ms = 12000U,
    };
  }

  [[nodiscard]] services::CapabilityTargetRef make_execution_target(
      std::string target_id = "loopback.target") const {
    return services::CapabilityTargetRef{
        .capability_id = options_.execution_capability_id,
        .target_id = std::move(target_id),
    };
  }

  [[nodiscard]] services::ExecutionCommandRequest make_execute_request(
      std::string request_id = "req-loopback-exec",
      std::string target_id = "loopback.target",
      std::string action = "toggle",
      std::string arguments_json = "{\"state\":\"on\"}") const {
    return services::ExecutionCommandRequest{
        .context = make_context(std::move(request_id)),
        .target = make_execution_target(std::move(target_id)),
        .action = std::move(action),
        .arguments_json = std::move(arguments_json),
        .idempotency_key = std::nullopt,
    };
  }

  [[nodiscard]] services::DataQueryRequest make_query_request(
      std::string request_id = "req-loopback-query",
      std::string dataset = {},
      std::string projection = "status",
      services::ServiceDataFreshness freshness = services::ServiceDataFreshness::strict) const {
    return services::DataQueryRequest{
        .context = make_context(std::move(request_id)),
        .dataset = dataset.empty() ? options_.data_capability_id : std::move(dataset),
        .filters_json = "{}",
        .projection = std::move(projection),
        .freshness = freshness,
    };
  }

  [[nodiscard]] services::DataCatalogRequest make_catalog_request(
      std::string request_id = "req-loopback-catalog",
      std::string target_class = {}) const {
    return services::DataCatalogRequest{
        .context = make_context(std::move(request_id)),
        .target_class = target_class.empty() ? options_.data_capability_id
                                             : std::move(target_class),
    };
  }

  [[nodiscard]] services::ExecutionSubscriptionRequest make_subscription_request(
      std::string request_id = "req-loopback-subscribe",
      std::string target_id = "loopback.target",
      std::string stream_kind = "status",
      std::optional<std::string> cursor = std::nullopt,
      std::uint32_t max_events = 2U) const {
    return services::ExecutionSubscriptionRequest{
        .context = make_context(std::move(request_id)),
        .target = make_execution_target(std::move(target_id)),
        .stream_kind = std::move(stream_kind),
        .cursor = std::move(cursor),
        .max_events = max_events,
    };
  }

  void publish_subscription_events(
      std::string target_id,
      std::string stream_kind,
      const std::vector<std::string>& events_json_batch) {
    subscription_hub_->publish(make_execution_target(std::move(target_id)),
                               std::move(stream_kind),
                               events_json_batch);
  }

  void set_now_ms(std::uint64_t now_ms) {
    options_.now_ms = now_ms;
  }

  [[nodiscard]] const std::vector<services::internal::AdapterInvocationRequest>& local_requests()
      const {
    return local_requests_;
  }

  [[nodiscard]] const std::vector<services::internal::AdapterInvocationRequest>& remote_requests()
      const {
    return remote_requests_;
  }

 private:
  [[nodiscard]] std::function<std::vector<std::string>(
      const std::string& capability_id,
      const std::string& action,
      const std::string& capability_version,
      const services::internal::AdapterReceipt& receipt)>
  build_compensation_lookup() const {
    if (options_.lookup_compensation_hints) {
      return options_.lookup_compensation_hints;
    }

    return [](const std::string&,
              const std::string&,
              const std::string&,
              const services::internal::AdapterReceipt&) {
      return std::vector<std::string>{};
    };
  }

  [[nodiscard]] bool wants_remote_route() const {
    return options_.remote_service_available || options_.remote_timeout ||
           static_cast<bool>(options_.remote_handler);
  }

  [[nodiscard]] std::vector<services::internal::AdapterRouteKind> build_route_order() const {
    std::vector<services::internal::AdapterRouteKind> route_order{
        services::internal::AdapterRouteKind::local_service,
    };
    if (wants_remote_route()) {
      route_order.push_back(services::internal::AdapterRouteKind::remote_service);
    }
    return route_order;
  }

  [[nodiscard]] std::vector<const services::internal::IAdapterInvoker*> build_invokers() const {
    std::vector<const services::internal::IAdapterInvoker*> invokers;
    invokers.push_back(local_service_adapter_.get());
    if (wants_remote_route()) {
      invokers.push_back(remote_service_adapter_.get());
    }
    return invokers;
  }

  [[nodiscard]] services::internal::ServicePolicyView build_policy_view() const {
    services::internal::ServicePolicyView policy_view{};
    policy_view.effective_profile_id = options_.profile_id;
    policy_view.default_allow_stale_reads = true;
    policy_view.read_path_degrade_allowed = options_.allow_route_degrade;
    policy_view.adapter_preference_order = build_route_order();
    return policy_view;
  }

  [[nodiscard]] services::internal::CapabilitySnapshotView build_execution_snapshot() const {
    const auto route_order = build_route_order();
    return services::internal::CapabilitySnapshotView{
        .capability_id = options_.execution_capability_id,
        .capability_version = "v1",
        .supported_actions = {"toggle"},
        .supported_queries = {},
        .route_classes = route_order,
        .preferred_locality = route_order.front(),
    };
  }

  [[nodiscard]] services::internal::CapabilitySnapshotView build_data_snapshot() const {
    const auto route_order = build_route_order();
    return services::internal::CapabilitySnapshotView{
        .capability_id = options_.data_capability_id,
        .capability_version = "v1",
        .supported_actions = {},
        .supported_queries = {"status", "catalog.list"},
        .route_classes = route_order,
        .preferred_locality = route_order.front(),
    };
  }

  [[nodiscard]] services::internal::FallbackEnvelope build_fallback_envelope(
      std::string action_class) const {
    return services::internal::FallbackEnvelope{
        .requested_action_class = std::move(action_class),
        .ordered_candidates = build_route_order(),
        .route_equivalence_class = "service.loopback",
        .allow_degrade = options_.allow_route_degrade,
        .deny_reason_on_exhaustion = "fallback_blocked",
    };
  }

  [[nodiscard]] std::vector<services::internal::AdapterCandidateView> build_candidates() const {
    std::vector<services::internal::AdapterCandidateView> candidates;
    candidates.push_back(services::internal::AdapterCandidateView{
        .adapter_id = "loopback.local_service",
        .route_kind = services::internal::AdapterRouteKind::local_service,
        .route_equivalence_class = "service.loopback",
        .trust_class = services::internal::AdapterTrustClass::caller_verified,
        .availability_state = options_.local_service_available
                                  ? services::internal::AdapterAvailabilityState::available
                                  : services::internal::AdapterAvailabilityState::unavailable,
        .supported_capabilities = {options_.execution_capability_id,
                                   options_.data_capability_id},
    });

    if (wants_remote_route()) {
      candidates.push_back(services::internal::AdapterCandidateView{
          .adapter_id = "loopback.remote_service",
          .route_kind = services::internal::AdapterRouteKind::remote_service,
          .route_equivalence_class = "service.loopback",
          .trust_class = services::internal::AdapterTrustClass::caller_verified,
          .availability_state = services::internal::AdapterAvailabilityState::available,
          .supported_capabilities = {options_.execution_capability_id,
                                     options_.data_capability_id},
      });
    }

    return candidates;
  }

  [[nodiscard]] static services::internal::AdapterInvocationResult make_default_local_result(
      const services::internal::AdapterInvocationRequest& request) {
    if (request.request_kind == services::internal::AdapterRouteRequestKind::action) {
      return services::internal::AdapterInvocationResult{
          .transport_outcome = services::internal::AdapterTransportOutcome::acknowledged,
          .provider_status_code = "ok",
          .payload_json = std::string("{\"applied\":true,\"operation\":\"") +
                          request.operation_name + "\"}",
          .latency_ms = 5U,
          .side_effects = {request.operation_name + ".applied"},
          .evidence_refs = {std::string("loopback://local/action/") + request.operation_name},
      };
    }

    if (request.operation_name == "catalog.list") {
      return services::internal::AdapterInvocationResult{
          .transport_outcome = services::internal::AdapterTransportOutcome::acknowledged,
          .provider_status_code = "ok",
          .payload_json = std::string("{\"target_class\":\"") + request.capability_id +
                          "\",\"routes\":[\"local_service\"]}",
          .latency_ms = 3U,
          .side_effects = {},
          .evidence_refs = {std::string("loopback://local/catalog/") + request.capability_id},
      };
    }

    return services::internal::AdapterInvocationResult{
        .transport_outcome = services::internal::AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = std::string("[{\"capability_id\":\"") + request.capability_id +
                        "\",\"target_id\":\"" + request.target_id +
                        "\",\"projection\":\"" + request.operation_name + "\"}]",
        .latency_ms = 4U,
        .side_effects = {},
        .evidence_refs = {std::string("loopback://local/query/") + request.operation_name},
    };
  }

  [[nodiscard]] static services::internal::AdapterInvocationResult make_default_remote_result(
      const services::internal::AdapterInvocationRequest& request) {
    if (request.request_kind == services::internal::AdapterRouteRequestKind::action) {
      return services::internal::AdapterInvocationResult{
          .transport_outcome = services::internal::AdapterTransportOutcome::acknowledged,
          .provider_status_code = "accepted",
          .payload_json = std::string("{\"applied\":true,\"remote\":true,\"operation\":\"") +
                          request.operation_name + "\"}",
          .latency_ms = 11U,
          .side_effects = {request.operation_name + ".remote_applied"},
          .evidence_refs = {std::string("loopback://remote/action/") + request.operation_name},
      };
    }

    if (request.operation_name == "catalog.list") {
      return services::internal::AdapterInvocationResult{
          .transport_outcome = services::internal::AdapterTransportOutcome::acknowledged,
          .provider_status_code = "accepted",
          .payload_json = std::string("{\"target_class\":\"") + request.capability_id +
                          "\",\"routes\":[\"remote_service\"]}",
          .latency_ms = 9U,
          .side_effects = {},
          .evidence_refs = {std::string("loopback://remote/catalog/") + request.capability_id},
      };
    }

    return services::internal::AdapterInvocationResult{
        .transport_outcome = services::internal::AdapterTransportOutcome::acknowledged,
        .provider_status_code = "accepted",
        .payload_json = std::string("[{\"capability_id\":\"") + request.capability_id +
                        "\",\"target_id\":\"" + request.target_id +
                        "\",\"projection\":\"" + request.operation_name +
                        "\",\"remote\":true}]",
        .latency_ms = 8U,
        .side_effects = {},
        .evidence_refs = {std::string("loopback://remote/query/") + request.operation_name},
    };
  }

  CapabilityServicesLoopbackFixtureOptions options_;
  std::vector<services::internal::AdapterInvocationRequest> local_requests_;
  std::vector<services::internal::AdapterInvocationRequest> remote_requests_;
  services::internal::AdapterRouter router_;
  services::internal::ResultMapper result_mapper_;
  services::internal::ServiceContextBuilder context_builder_;
  std::unique_ptr<services::internal::LocalServiceAdapter> local_service_adapter_;
  std::unique_ptr<services::internal::RemoteServiceAdapter> remote_service_adapter_;
  std::unique_ptr<services::internal::AdapterBridge> bridge_;
  std::unique_ptr<services::internal::DataProjectionCache> projection_cache_;
  std::unique_ptr<services::internal::ExecutionCommandLane> command_lane_;
  std::unique_ptr<services::internal::DataQueryLane> data_query_lane_;
  std::unique_ptr<services::internal::ExecutionSubscriptionHub> subscription_hub_;
  std::unique_ptr<services::internal::ServiceFacade> facade_;
};

}  // namespace dasall::tests::mocks
#include "ServiceLiveComposition.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "ServiceFacade.h"
#include "ServiceContextBuilder.h"
#include "adapters/AdapterBridge.h"
#include "adapters/AdapterRouter.h"
#include "adapters/LocalServiceAdapter.h"
#include "adapters/RemoteServiceAdapter.h"
#include "bridges/ServiceAuditBridge.h"
#include "bridges/ServiceMetricsBridge.h"
#include "bridges/ServiceTraceBridge.h"
#include "data/DataProjectionCache.h"
#include "data/DataQueryLane.h"
#include "execution/ExecutionCommandLane.h"
#include "mapping/ResultMapper.h"
#include "ops/ServiceConfigAdapter.h"
#include "ops/ServiceHealthProbe.h"

namespace dasall::services {
namespace {

using internal::AdapterAvailabilityState;
using internal::AdapterBridge;
using internal::AdapterBridgeDependencies;
using internal::AdapterCandidateView;
using internal::AdapterInvocationRequest;
using internal::AdapterInvocationResult;
using internal::AdapterRouteKind;
using internal::AdapterRouteRequestKind;
using internal::AdapterSelection;
using internal::AdapterTransportOutcome;
using internal::CapabilitySnapshotView;
using internal::DataProjectionCache;
using internal::DataQueryLane;
using internal::DataQueryLaneDependencies;
using internal::ExecutionCommandLane;
using internal::ExecutionCommandLaneDependencies;
using internal::FallbackEnvelope;
using internal::LocalServiceAdapter;
using internal::LocalServiceAdapterOptions;
using internal::RemoteServiceAdapter;
using internal::RemoteServiceAdapterOptions;
using internal::ResultMapper;
using internal::ServiceAuditBridge;
using internal::ServiceConfigAdapter;
using internal::ServiceFacade;
using internal::ServiceFacadeDependencies;
using internal::ServiceHealthProbe;
using internal::ServiceHealthSample;
using internal::ServicePolicyView;
using internal::ServiceMetricsBridge;
using internal::ServiceTraceBridge;

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] bool wants_remote_route(const ServiceLiveCompositionOptions& options) {
  return options.remote_service_available || options.remote_timeout;
}

[[nodiscard]] profiles::BuildProfileManifest build_manifest(
    const ServiceLiveCompositionOptions& options) {
  profiles::BuildProfileManifest build_manifest{
      .enabled_modules = {"runtime", "services", "tools_builtin"},
      .enabled_adapters = {"local_service"},
      .observability_level = options.observability_level,
      .build_tags = {"services:live"},
      .toolchain_hint = options.toolchain_hint.empty()
          ? std::nullopt
          : std::optional<std::string>(options.toolchain_hint),
  };

  if (options.local_platform_route_enabled) {
    build_manifest.enabled_modules.push_back("platform_hal");
    build_manifest.enabled_adapters.push_back("local_platform");
  }
  if (options.observability_enabled) {
    build_manifest.enabled_modules.push_back("infra_observability");
    build_manifest.build_tags.push_back("services:observability");
  }
  if (wants_remote_route(options)) {
    build_manifest.enabled_adapters.push_back("remote_service");
  }

  return build_manifest;
}

[[nodiscard]] std::vector<AdapterRouteKind> build_route_order(
    const ServicePolicyView& policy_view,
    const ServiceLiveCompositionOptions& options) {
  if (!policy_view.adapter_preference_order.empty()) {
    return policy_view.adapter_preference_order;
  }

  std::vector<AdapterRouteKind> route_order;
  if (options.local_platform_route_enabled) {
    route_order.push_back(AdapterRouteKind::local_platform);
  }
  route_order.push_back(AdapterRouteKind::local_service);
  if (wants_remote_route(options)) {
    route_order.push_back(AdapterRouteKind::remote_service);
  }
  return route_order;
}

[[nodiscard]] CapabilitySnapshotView build_execution_snapshot(
    const ServiceLiveCompositionOptions& options,
    const std::vector<AdapterRouteKind>& route_order) {
  return CapabilitySnapshotView{
      .capability_id = options.execution_capability_id,
      .capability_version = "v1",
      .supported_actions = {options.execution_capability_id},
      .supported_queries = {},
      .route_classes = route_order,
      .preferred_locality = route_order.empty()
          ? std::optional<AdapterRouteKind>{}
          : std::optional<AdapterRouteKind>(route_order.front()),
  };
}

[[nodiscard]] CapabilitySnapshotView build_data_snapshot(
    const ServiceLiveCompositionOptions& options,
    const std::vector<AdapterRouteKind>& route_order) {
  return CapabilitySnapshotView{
      .capability_id = options.data_capability_id,
      .capability_version = "v1",
      .supported_actions = {},
      .supported_queries = {"default", "catalog.list"},
      .route_classes = route_order,
      .preferred_locality = route_order.empty()
          ? std::optional<AdapterRouteKind>{}
          : std::optional<AdapterRouteKind>(route_order.front()),
  };
}

[[nodiscard]] FallbackEnvelope build_fallback_envelope(
    std::string action_class,
    const std::vector<AdapterRouteKind>& route_order,
    bool allow_route_degrade) {
  return FallbackEnvelope{
      .requested_action_class = std::move(action_class),
      .ordered_candidates = route_order,
      .route_equivalence_class = "service.live",
      .allow_degrade = allow_route_degrade,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] std::vector<AdapterCandidateView> build_candidates(
    const ServiceLiveCompositionOptions& options) {
  std::vector<AdapterCandidateView> candidates;
  candidates.push_back(AdapterCandidateView{
      .adapter_id = "live.local_service",
      .route_kind = AdapterRouteKind::local_service,
      .route_equivalence_class = "service.live",
      .trust_class = internal::AdapterTrustClass::caller_verified,
      .availability_state = options.local_service_available
          ? AdapterAvailabilityState::available
          : AdapterAvailabilityState::unavailable,
      .supported_capabilities = {
          options.execution_capability_id,
          options.data_capability_id,
      },
  });

  if (wants_remote_route(options)) {
    candidates.push_back(AdapterCandidateView{
        .adapter_id = "live.remote_service",
        .route_kind = AdapterRouteKind::remote_service,
        .route_equivalence_class = "service.live",
        .trust_class = internal::AdapterTrustClass::caller_verified,
        .availability_state = options.remote_service_available
            ? AdapterAvailabilityState::available
            : AdapterAvailabilityState::degraded,
        .supported_capabilities = {
            options.execution_capability_id,
            options.data_capability_id,
        },
    });
  }

  return candidates;
}

[[nodiscard]] AdapterInvocationResult make_default_local_result(
    const AdapterInvocationRequest& request) {
  if (request.request_kind == AdapterRouteRequestKind::action) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = std::string("{\"applied\":true,\"operation\":\"") +
                        request.operation_name + "\"}",
        .latency_ms = 5U,
        .side_effects = {request.operation_name + ".applied"},
        .evidence_refs = {std::string("live://local/action/") + request.operation_name},
    };
  }

  if (request.operation_name == "catalog.list") {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = std::string("{\"target_class\":\"") + request.capability_id +
                        "\",\"routes\":[\"local_service\"]}",
        .latency_ms = 3U,
        .side_effects = {},
        .evidence_refs = {std::string("live://local/catalog/") + request.capability_id},
    };
  }

  return AdapterInvocationResult{
      .transport_outcome = AdapterTransportOutcome::acknowledged,
      .provider_status_code = "ok",
      .payload_json = std::string("[{\"capability_id\":\"") + request.capability_id +
                      "\",\"target_id\":\"" + request.target_id +
                      "\",\"projection\":\"" + request.operation_name + "\"}]",
      .latency_ms = 4U,
      .side_effects = {},
      .evidence_refs = {std::string("live://local/query/") + request.operation_name},
  };
}

[[nodiscard]] AdapterInvocationResult make_default_remote_result(
    const AdapterInvocationRequest& request) {
  if (request.request_kind == AdapterRouteRequestKind::action) {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "accepted",
        .payload_json = std::string("{\"applied\":true,\"remote\":true,\"operation\":\"") +
                        request.operation_name + "\"}",
        .latency_ms = 11U,
        .side_effects = {request.operation_name + ".remote_applied"},
        .evidence_refs = {std::string("live://remote/action/") + request.operation_name},
    };
  }

  if (request.operation_name == "catalog.list") {
    return AdapterInvocationResult{
        .transport_outcome = AdapterTransportOutcome::acknowledged,
        .provider_status_code = "accepted",
        .payload_json = std::string("{\"target_class\":\"") + request.capability_id +
                        "\",\"routes\":[\"remote_service\"]}",
        .latency_ms = 9U,
        .side_effects = {},
        .evidence_refs = {std::string("live://remote/catalog/") + request.capability_id},
    };
  }

  return AdapterInvocationResult{
      .transport_outcome = AdapterTransportOutcome::acknowledged,
      .provider_status_code = "accepted",
      .payload_json = std::string("[{\"capability_id\":\"") + request.capability_id +
                      "\",\"target_id\":\"" + request.target_id +
                      "\",\"projection\":\"" + request.operation_name +
                      "\",\"remote\":true}]",
      .latency_ms = 8U,
      .side_effects = {},
      .evidence_refs = {std::string("live://remote/query/") + request.operation_name},
  };
}

class LiveServiceCompositionRoot final : public IExecutionService,
                     public IDataService,
                     public internal::IServiceHealthSignalProvider,
                     public std::enable_shared_from_this<
                       LiveServiceCompositionRoot> {
 public:
  static ServiceLiveCompositionResult create(
      const profiles::RuntimePolicySnapshot& runtime_policy,
      const ServiceLiveCompositionOptions& options) {
    auto root = std::shared_ptr<LiveServiceCompositionRoot>(new LiveServiceCompositionRoot());
    const auto error = root->initialize(runtime_policy, options);
    if (!error.empty()) {
      return ServiceLiveCompositionResult{
          .execution_service = nullptr,
          .data_service = nullptr,
          .health_probe = nullptr,
          .error = error,
      };
    }

    return ServiceLiveCompositionResult{
        .execution_service = root,
        .data_service = root,
        .health_probe = root->health_probe_,
        .error = {},
    };
  }

  ExecutionCommandResult execute(const ExecutionCommandRequest& request) override {
    return facade_->execute(request);
  }

  ExecutionCommandResult compensate(const ExecutionCompensationRequest& request) override {
    return facade_->compensate(request);
  }

  ExecutionQueryResult query_state(const ExecutionQueryRequest& request) override {
    return facade_->query_state(request);
  }

  ExecutionSubscriptionResult subscribe(const ExecutionSubscriptionRequest& request) override {
    return facade_->subscribe(request);
  }

  ExecutionDiagnoseResult diagnose(const ExecutionDiagnoseRequest& request) override {
    return facade_->diagnose(request);
  }

  DataQueryResult query(const DataQueryRequest& request) override {
    return facade_->query(request);
  }

  DataCatalogResult list_capabilities(const DataCatalogRequest& request) override {
    return facade_->list_capabilities(request);
  }

  ServiceHealthSample sample(std::int64_t) override {
    ServiceHealthSample sample;
    sample.circuit_state = internal::ServiceCircuitState::closed;
    sample.adapter_readiness =
        (options_.local_service_available || options_.remote_service_available)
        ? AdapterAvailabilityState::available
        : AdapterAvailabilityState::unavailable;
    sample.audit_bridge_degraded = options_.observability_enabled &&
        (audit_logger_owner_ == nullptr || audit_bridge_ == nullptr ||
         audit_bridge_->get_status().degraded);
    sample.metrics_bridge_degraded = options_.observability_enabled &&
        (metrics_provider_owner_ == nullptr || metrics_bridge_ == nullptr ||
         metrics_bridge_->is_degraded());
    sample.trace_bridge_degraded = options_.observability_enabled &&
        (tracer_provider_owner_ == nullptr || trace_bridge_ == nullptr ||
         trace_bridge_->is_degraded());
    sample.latency_ms = 0;
    sample.sampled_at_unix_ms = current_time_ms();
    sample.detail_ref = "status://services/health/live-composition";
    return sample;
  }

 private:
  LiveServiceCompositionRoot() = default;

  [[nodiscard]] std::string initialize(
      const profiles::RuntimePolicySnapshot& runtime_policy,
      const ServiceLiveCompositionOptions& options) {
    options_ = options;
    const ServiceConfigAdapter config_adapter;
    const auto policy_result = config_adapter.derive_policy_view(
        runtime_policy,
        build_manifest(options));
    if (!policy_result.ok()) {
      return std::string("services policy derivation failed: ") + policy_result.error;
    }

    const ServicePolicyView policy_view = *policy_result.policy_view;
    const auto route_order = build_route_order(policy_view, options);
    const auto registered_candidates = build_candidates(options);

    if (options.observability_enabled) {
      if (options.audit_logger == nullptr || options.metrics_provider == nullptr ||
          options.tracer_provider == nullptr) {
        return "services observability composition requires audit, metrics, and trace providers";
      }

      audit_logger_owner_ = options.audit_logger;
      metrics_provider_owner_ = options.metrics_provider;
      tracer_provider_owner_ = options.tracer_provider;
      audit_bridge_ = std::make_unique<ServiceAuditBridge>(audit_logger_owner_.get());
      metrics_bridge_ = std::make_unique<ServiceMetricsBridge>(
          metrics_provider_owner_,
          internal::ServiceMetricsBridgeOptions{
              .enabled = true,
              .profile_id = runtime_policy.effective_profile_id(),
              .metrics_granularity = runtime_policy.ops_policy().metrics_granularity,
              .now_ms = []() {
                return current_time_ms();
              },
          });
      trace_bridge_ = std::make_unique<ServiceTraceBridge>(
          tracer_provider_owner_,
          internal::ServiceTraceBridgeOptions{
              .enabled = true,
              .profile_id = runtime_policy.effective_profile_id(),
              .trace_sample_ratio = runtime_policy.ops_policy().trace_sample_ratio,
          });
    }

    local_service_adapter_ = std::make_unique<LocalServiceAdapter>(
        LocalServiceAdapterOptions{
            .service_endpoint_available = options.local_service_available,
            .adapter_id = "live.local_service",
            .invoke_service = [](const AdapterInvocationRequest& request) {
              return make_default_local_result(request);
            },
        });

    if (wants_remote_route(options)) {
      remote_service_adapter_ = std::make_unique<RemoteServiceAdapter>(
          RemoteServiceAdapterOptions{
              .remote_endpoint_available = options.remote_service_available,
              .timeout_on_invoke = options.remote_timeout,
              .adapter_id = "live.remote_service",
              .invoke_remote = [](const AdapterInvocationRequest& request) {
                return make_default_remote_result(request);
              },
          });
    }

    bridge_ = std::make_unique<AdapterBridge>(AdapterBridgeDependencies{
        .invokers = build_invokers(),
      .trace_bridge = trace_bridge_.get(),
    });

    projection_cache_ = std::make_unique<DataProjectionCache>(
        internal::DataProjectionCacheDependencies{
            .ttl_ms = policy_view.data_cache_ttl_ms > 0
                ? static_cast<std::uint64_t>(policy_view.data_cache_ttl_ms)
                : 5000U,
            .now_ms = []() {
              return current_time_ms();
            },
        });

    command_lane_ = std::make_unique<ExecutionCommandLane>(ExecutionCommandLaneDependencies{
        .router = &router_,
        .bridge = bridge_.get(),
        .result_mapper = &result_mapper_,
        .compensation_catalog = nullptr,
        .policy_view = policy_view,
        .capability_snapshot = build_execution_snapshot(options, route_order),
        .fallback_envelope = build_fallback_envelope(
            "command.standard",
            route_order,
            options.allow_route_degrade),
        .registered_candidates = registered_candidates,
        .critical_actions = options.critical_actions,
        .high_risk_actions = options.high_risk_actions,
        .allow_high_risk_actions = true,
        .lookup_compensation_hints = [](const std::string&,
                                        const std::string&,
                                        const std::string&,
                                        const internal::AdapterReceipt&) {
          return std::vector<std::string>{};
        },
        .make_execution_id = {},
        .make_compensation_execution_id = {},
        .on_serialization_acquired = {},
        .audit_bridge = audit_bridge_.get(),
        .metrics_bridge = metrics_bridge_.get(),
        .trace_bridge = trace_bridge_.get(),
    });

    data_query_lane_ = std::make_unique<DataQueryLane>(DataQueryLaneDependencies{
        .router = &router_,
        .bridge = bridge_.get(),
        .result_mapper = &result_mapper_,
        .projection_cache = projection_cache_.get(),
        .policy_view = policy_view,
        .capability_snapshot = build_data_snapshot(options, route_order),
        .fallback_envelope = build_fallback_envelope(
            "query.read_only",
            route_order,
            options.allow_route_degrade),
        .registered_candidates = registered_candidates,
        .metrics_bridge = metrics_bridge_.get(),
        .trace_bridge = trace_bridge_.get(),
    });

    facade_ = std::make_unique<ServiceFacade>(ServiceFacadeDependencies{
        .context_builder = &context_builder_,
        .execute_command = [this](const ServiceCallContext& context,
                                  const ExecutionCommandRequest& request) {
          return command_lane_->execute(context, request);
        },
        .compensate_command = [this](const ServiceCallContext& context,
                                     const ExecutionCompensationRequest& request) {
          return command_lane_->compensate(context, request);
        },
        .query_execution_state = {},
        .subscribe_execution_state = {},
        .diagnose_execution_target = {},
        .query_data = [this](const ServiceCallContext& context,
                             const DataQueryRequest& request) {
          return data_query_lane_->query(context, request);
        },
        .list_data_capabilities = [this](const ServiceCallContext& context,
                                         const DataCatalogRequest& request) {
          return data_query_lane_->list_capabilities(context, request);
        },
        .trace_bridge = trace_bridge_.get(),
    });

    if (options.health_probe_enabled) {
      health_probe_ = std::make_shared<ServiceHealthProbe>(
          std::static_pointer_cast<internal::IServiceHealthSignalProvider>(
              shared_from_this()),
          internal::ServiceHealthProbeOptions{
              .now_ms = []() {
                return current_time_ms();
              },
          });
    }

    return {};
  }

  [[nodiscard]] std::vector<const internal::IAdapterInvoker*> build_invokers() const {
    std::vector<const internal::IAdapterInvoker*> invokers;
    invokers.push_back(local_service_adapter_.get());
    if (remote_service_adapter_ != nullptr) {
      invokers.push_back(remote_service_adapter_.get());
    }
    return invokers;
  }

  internal::AdapterRouter router_;
  ResultMapper result_mapper_;
  internal::ServiceContextBuilder context_builder_;
  ServiceLiveCompositionOptions options_{};
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger_owner_;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_owner_;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider_owner_;
  std::unique_ptr<ServiceAuditBridge> audit_bridge_;
  std::unique_ptr<ServiceMetricsBridge> metrics_bridge_;
  std::unique_ptr<ServiceTraceBridge> trace_bridge_;
  std::unique_ptr<LocalServiceAdapter> local_service_adapter_;
  std::unique_ptr<RemoteServiceAdapter> remote_service_adapter_;
  std::unique_ptr<AdapterBridge> bridge_;
  std::unique_ptr<DataProjectionCache> projection_cache_;
  std::unique_ptr<ExecutionCommandLane> command_lane_;
  std::unique_ptr<DataQueryLane> data_query_lane_;
  std::unique_ptr<ServiceFacade> facade_;
  std::shared_ptr<ServiceHealthProbe> health_probe_;
};

}  // namespace

ServiceLiveCompositionResult compose_live_services(
    const profiles::RuntimePolicySnapshot& runtime_policy,
    const ServiceLiveCompositionOptions& options) {
  return LiveServiceCompositionRoot::create(runtime_policy, options);
}

}  // namespace dasall::services
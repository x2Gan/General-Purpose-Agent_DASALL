#include "ServiceLiveComposition.h"

#include <chrono>
#include <cstdint>
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
#include "adapters/LocalPlatformAdapter.h"
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
using internal::CapabilityRouteView;
using internal::CapabilitySnapshotView;
using internal::DataProjectionCache;
using internal::DataQueryLane;
using internal::DataQueryLaneDependencies;
using internal::ExecutionCommandLane;
using internal::ExecutionCommandLaneDependencies;
using internal::FallbackEnvelope;
using internal::LocalPlatformAdapter;
using internal::LocalPlatformAdapterOptions;
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

[[nodiscard]] bool has_explicit_adapter_registry(const ServiceLiveCompositionOptions& options) {
  return options.adapter_registry.has_value();
}

[[nodiscard]] bool has_local_platform_binding(
    const ServiceLiveCompositionOptions& options) {
  return !has_explicit_adapter_registry(options) ||
         options.adapter_registry->local_platform.has_value();
}

[[nodiscard]] bool has_local_service_binding(
    const ServiceLiveCompositionOptions& options) {
  return !has_explicit_adapter_registry(options) ||
         options.adapter_registry->local_service.has_value();
}

[[nodiscard]] bool wants_remote_route(const ServiceLiveCompositionOptions& options) {
  if (has_explicit_adapter_registry(options)) {
    return options.adapter_registry->remote_service.has_value();
  }

  return options.remote_service_available || options.remote_timeout;
}

[[nodiscard]] profiles::BuildProfileManifest build_manifest(
    const ServiceLiveCompositionOptions& options) {
  profiles::BuildProfileManifest build_manifest{
      .enabled_modules = {"runtime", "services", "tools_builtin"},
      .enabled_adapters = {},
      .observability_level = options.observability_level,
      .build_tags = {"services:live"},
      .toolchain_hint = options.toolchain_hint.empty()
          ? std::nullopt
          : std::optional<std::string>(options.toolchain_hint),
  };

  if (has_local_service_binding(options)) {
    build_manifest.enabled_adapters.push_back("local_service");
  }
  if (options.local_platform_route_enabled && has_local_platform_binding(options)) {
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
  if (has_local_service_binding(options)) {
    route_order.push_back(AdapterRouteKind::local_service);
  }
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
    bool allow_route_degrade,
    std::string route_equivalence_class = "service.live") {
  return FallbackEnvelope{
      .requested_action_class = std::move(action_class),
      .ordered_candidates = route_order,
      .route_equivalence_class = route_equivalence_class.empty()
          ? std::string("service.live")
          : std::move(route_equivalence_class),
      .allow_degrade = allow_route_degrade,
      .deny_reason_on_exhaustion = "fallback_blocked",
  };
}

[[nodiscard]] int availability_rank(AdapterAvailabilityState availability_state) {
  switch (availability_state) {
    case AdapterAvailabilityState::available:
      return 3;
    case AdapterAvailabilityState::degraded:
      return 2;
    case AdapterAvailabilityState::unavailable:
      return 1;
    case AdapterAvailabilityState::unknown:
      return 0;
  }

  return 0;
}

[[nodiscard]] std::string default_adapter_id(ServiceLiveRouteKind route_kind) {
  switch (route_kind) {
    case ServiceLiveRouteKind::local_platform:
      return "live.local_platform";
    case ServiceLiveRouteKind::local_service:
      return "live.local_service";
    case ServiceLiveRouteKind::remote_service:
      return "live.remote_service";
  }

  return "live.unknown";
}

[[nodiscard]] std::vector<std::string> default_supported_capabilities(
    ServiceLiveRouteKind route_kind,
    const ServiceLiveCompositionOptions& options) {
  if (route_kind == ServiceLiveRouteKind::local_platform) {
    return {options.execution_capability_id};
  }

  return {
      options.execution_capability_id,
      options.data_capability_id,
  };
}

[[nodiscard]] std::string route_equivalence_class_for(
    const ServiceLiveAdapterRegistry& registry) {
  return registry.route_equivalence_class.empty() ? "service.live"
                                                  : registry.route_equivalence_class;
}

[[nodiscard]] std::string adapter_id_for(const ServiceLiveRouteBinding& binding,
                                         ServiceLiveRouteKind route_kind) {
  return binding.adapter_id.empty() ? default_adapter_id(route_kind)
                                    : binding.adapter_id;
}

[[nodiscard]] std::vector<std::string> supported_capabilities_for(
    const ServiceLiveRouteBinding& binding,
    ServiceLiveRouteKind route_kind,
    const ServiceLiveCompositionOptions& options) {
  if (!binding.supported_capabilities.empty()) {
    return binding.supported_capabilities;
  }

  return default_supported_capabilities(route_kind, options);
}

[[nodiscard]] bool endpoint_available_for(
    ServiceLiveAvailabilityState availability_state) {
  switch (availability_state) {
    case ServiceLiveAvailabilityState::available:
    case ServiceLiveAvailabilityState::degraded:
      return true;
    case ServiceLiveAvailabilityState::unavailable:
    case ServiceLiveAvailabilityState::unknown:
      return false;
  }

  return false;
}

[[nodiscard]] AdapterRouteKind to_internal_route_kind(
    ServiceLiveRouteKind route_kind) {
  switch (route_kind) {
    case ServiceLiveRouteKind::local_platform:
      return AdapterRouteKind::local_platform;
    case ServiceLiveRouteKind::local_service:
      return AdapterRouteKind::local_service;
    case ServiceLiveRouteKind::remote_service:
      return AdapterRouteKind::remote_service;
  }

  return AdapterRouteKind::local_service;
}

[[nodiscard]] internal::AdapterTrustClass to_internal_trust_class(
    ServiceLiveTrustClass trust_class) {
  switch (trust_class) {
    case ServiceLiveTrustClass::untrusted:
      return internal::AdapterTrustClass::untrusted;
    case ServiceLiveTrustClass::caller_verified:
      return internal::AdapterTrustClass::caller_verified;
    case ServiceLiveTrustClass::trusted_local:
      return internal::AdapterTrustClass::trusted_local;
  }

  return internal::AdapterTrustClass::untrusted;
}

[[nodiscard]] AdapterAvailabilityState to_internal_availability_state(
    ServiceLiveAvailabilityState availability_state) {
  switch (availability_state) {
    case ServiceLiveAvailabilityState::available:
      return AdapterAvailabilityState::available;
    case ServiceLiveAvailabilityState::degraded:
      return AdapterAvailabilityState::degraded;
    case ServiceLiveAvailabilityState::unavailable:
      return AdapterAvailabilityState::unavailable;
    case ServiceLiveAvailabilityState::unknown:
      return AdapterAvailabilityState::unknown;
  }

  return AdapterAvailabilityState::unknown;
}

[[nodiscard]] ServiceLiveRequestKind to_live_request_kind(
    AdapterRouteRequestKind request_kind) {
  return request_kind == AdapterRouteRequestKind::action
      ? ServiceLiveRequestKind::action
      : ServiceLiveRequestKind::query;
}

[[nodiscard]] AdapterTransportOutcome to_internal_transport_outcome(
    ServiceLiveTransportOutcome transport_outcome) {
  switch (transport_outcome) {
    case ServiceLiveTransportOutcome::acknowledged:
      return AdapterTransportOutcome::acknowledged;
    case ServiceLiveTransportOutcome::timeout:
      return AdapterTransportOutcome::timeout;
    case ServiceLiveTransportOutcome::unreachable:
      return AdapterTransportOutcome::unreachable;
    case ServiceLiveTransportOutcome::rejected:
      return AdapterTransportOutcome::rejected;
    case ServiceLiveTransportOutcome::partial:
      return AdapterTransportOutcome::partial;
  }

  return AdapterTransportOutcome::rejected;
}

[[nodiscard]] ServiceLiveBackendRequest to_live_backend_request(
    const AdapterInvocationRequest& request) {
  return ServiceLiveBackendRequest{
      .request_id = request.request_id,
      .capability_id = request.capability_id,
      .target_id = request.target_id,
      .request_kind = to_live_request_kind(request.request_kind),
      .operation_name = request.operation_name,
      .payload_json = request.payload_json,
  };
}

[[nodiscard]] AdapterInvocationResult to_adapter_invocation_result(
    const ServiceLiveBackendResult& result) {
  return AdapterInvocationResult{
      .transport_outcome = to_internal_transport_outcome(result.transport_outcome),
      .provider_status_code = result.provider_status_code,
      .payload_json = result.payload_json,
      .latency_ms = result.latency_ms,
      .side_effects = result.side_effects,
      .evidence_refs = result.evidence_refs,
  };
}

[[nodiscard]] std::function<AdapterInvocationResult(const AdapterInvocationRequest& request)>
make_backend_handler(const ServiceLiveBackendHandler& handler) {
  if (!handler) {
    return {};
  }

  return [handler](const AdapterInvocationRequest& request) {
    return to_adapter_invocation_result(handler(to_live_backend_request(request)));
  };
}

[[nodiscard]] AdapterAvailabilityState derive_adapter_readiness(
    const ServicePolicyView& policy_view,
    const std::vector<AdapterCandidateView>& registered_candidates) {
  int best_rank = -1;
  AdapterAvailabilityState best_state = AdapterAvailabilityState::unknown;

  for (const auto& candidate : registered_candidates) {
    if (candidate.route_kind == AdapterRouteKind::local_platform &&
        !policy_view.local_platform_route_enabled) {
      continue;
    }

    const auto rank = availability_rank(candidate.availability_state);
    if (rank <= best_rank) {
      continue;
    }

    best_rank = rank;
    best_state = candidate.availability_state;
  }

  return best_rank >= 0 ? best_state : AdapterAvailabilityState::unknown;
}

[[nodiscard]] ServiceLiveBackendResult make_default_platform_backend_result(
    const ServiceLiveBackendRequest& request) {
  return ServiceLiveBackendResult{
      .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
      .provider_status_code = "ok",
      .payload_json = std::string("{\"applied\":true,\"route\":\"local_platform\",\"operation\":\"") +
                      request.operation_name + "\"}",
      .latency_ms = 2U,
      .side_effects = {request.operation_name + ".platform_applied"},
      .evidence_refs = {std::string("live://platform/action/") +
                        request.operation_name},
  };
}

[[nodiscard]] ServiceLiveBackendResult make_default_local_backend_result(
    const ServiceLiveBackendRequest& request) {
  if (request.request_kind == ServiceLiveRequestKind::action) {
    return ServiceLiveBackendResult{
        .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = std::string("{\"applied\":true,\"operation\":\"") +
                        request.operation_name + "\"}",
        .latency_ms = 5U,
        .side_effects = {request.operation_name + ".applied"},
        .evidence_refs = {std::string("live://local/action/") +
                          request.operation_name},
    };
  }

  if (request.operation_name == "catalog.list") {
    return ServiceLiveBackendResult{
        .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
        .provider_status_code = "ok",
        .payload_json = std::string("{\"target_class\":\"") +
                        request.capability_id + "\",\"routes\":[\"local_service\"]}",
        .latency_ms = 3U,
        .side_effects = {},
        .evidence_refs = {std::string("live://local/catalog/") +
                          request.capability_id},
    };
  }

  return ServiceLiveBackendResult{
      .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
      .provider_status_code = "ok",
      .payload_json = std::string("[{\"capability_id\":\"") + request.capability_id +
                      "\",\"target_id\":\"" + request.target_id +
                      "\",\"projection\":\"" + request.operation_name + "\"}]",
      .latency_ms = 4U,
      .side_effects = {},
      .evidence_refs = {std::string("live://local/query/") +
                        request.operation_name},
  };
}

[[nodiscard]] ServiceLiveBackendResult make_default_remote_backend_result(
    const ServiceLiveBackendRequest& request) {
  if (request.request_kind == ServiceLiveRequestKind::action) {
    return ServiceLiveBackendResult{
        .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
        .provider_status_code = "accepted",
        .payload_json = std::string(
                            "{\"applied\":true,\"remote\":true,\"operation\":\"") +
                        request.operation_name + "\"}",
        .latency_ms = 11U,
        .side_effects = {request.operation_name + ".remote_applied"},
        .evidence_refs = {std::string("live://remote/action/") +
                          request.operation_name},
    };
  }

  if (request.operation_name == "catalog.list") {
    return ServiceLiveBackendResult{
        .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
        .provider_status_code = "accepted",
        .payload_json = std::string("{\"target_class\":\"") +
                        request.capability_id + "\",\"routes\":[\"remote_service\"]}",
        .latency_ms = 9U,
        .side_effects = {},
        .evidence_refs = {std::string("live://remote/catalog/") +
                          request.capability_id},
    };
  }

  return ServiceLiveBackendResult{
      .transport_outcome = ServiceLiveTransportOutcome::acknowledged,
      .provider_status_code = "accepted",
      .payload_json = std::string("[{\"capability_id\":\"") + request.capability_id +
                      "\",\"target_id\":\"" + request.target_id +
                      "\",\"projection\":\"" + request.operation_name +
                      "\",\"remote\":true}]",
      .latency_ms = 8U,
      .side_effects = {},
      .evidence_refs = {std::string("live://remote/query/") +
                        request.operation_name},
  };
}

[[nodiscard]] ServiceLiveAdapterRegistry build_default_adapter_registry(
    const ServiceLiveCompositionOptions& options) {
  ServiceLiveAdapterRegistry registry;

  if (options.local_platform_route_enabled) {
    registry.local_platform = ServiceLiveRouteBinding{
        .adapter_id = "live.local_platform",
        .trust_class = ServiceLiveTrustClass::trusted_local,
        .availability_state = ServiceLiveAvailabilityState::available,
        .supported_capabilities = {options.execution_capability_id},
        .handler = [](const ServiceLiveBackendRequest& request) {
          return make_default_platform_backend_result(request);
        },
        .timeout_on_invoke = false,
    };
  }

  registry.local_service = ServiceLiveRouteBinding{
      .adapter_id = "live.local_service",
      .trust_class = ServiceLiveTrustClass::caller_verified,
      .availability_state = options.local_service_available
          ? ServiceLiveAvailabilityState::available
          : ServiceLiveAvailabilityState::unavailable,
      .supported_capabilities = {
          options.execution_capability_id,
          options.data_capability_id,
      },
      .handler = [](const ServiceLiveBackendRequest& request) {
        return make_default_local_backend_result(request);
      },
      .timeout_on_invoke = false,
  };

  if (wants_remote_route(options)) {
    registry.remote_service = ServiceLiveRouteBinding{
        .adapter_id = "live.remote_service",
        .trust_class = ServiceLiveTrustClass::caller_verified,
        .availability_state = options.remote_service_available
            ? ServiceLiveAvailabilityState::available
            : ServiceLiveAvailabilityState::degraded,
        .supported_capabilities = {
            options.execution_capability_id,
            options.data_capability_id,
        },
        .handler = [](const ServiceLiveBackendRequest& request) {
          return make_default_remote_backend_result(request);
        },
        .timeout_on_invoke = options.remote_timeout,
    };
  }

  return registry;
}

[[nodiscard]] std::vector<AdapterCandidateView> build_candidates(
    const ServiceLiveCompositionOptions& options,
    const ServiceLiveAdapterRegistry& registry) {
  std::vector<AdapterCandidateView> candidates;
  const auto route_equivalence_class = route_equivalence_class_for(registry);

  const auto append_candidate = [&](const std::optional<ServiceLiveRouteBinding>& binding,
                                    ServiceLiveRouteKind route_kind) {
    if (!binding.has_value()) {
      return;
    }

    candidates.push_back(AdapterCandidateView{
        .adapter_id = adapter_id_for(*binding, route_kind),
        .route_kind = to_internal_route_kind(route_kind),
        .route_equivalence_class = route_equivalence_class,
        .trust_class = to_internal_trust_class(binding->trust_class),
        .availability_state = to_internal_availability_state(binding->availability_state),
        .supported_capabilities =
            supported_capabilities_for(*binding, route_kind, options),
    });
  };

  append_candidate(registry.local_platform, ServiceLiveRouteKind::local_platform);
  append_candidate(registry.local_service, ServiceLiveRouteKind::local_service);
  append_candidate(registry.remote_service, ServiceLiveRouteKind::remote_service);

  return candidates;
}

[[nodiscard]] bool candidate_supports_capability(const AdapterCandidateView& candidate,
                                                 const std::string& capability_id) {
  for (const auto& supported_capability : candidate.supported_capabilities) {
    if (supported_capability == capability_id) {
      return true;
    }
  }

  return false;
}

void append_route_kind_if_missing(std::vector<AdapterRouteKind>* route_classes,
                                  AdapterRouteKind route_kind) {
  for (const auto existing_route_kind : *route_classes) {
    if (existing_route_kind == route_kind) {
      return;
    }
  }

  route_classes->push_back(route_kind);
}

[[nodiscard]] std::vector<AdapterCandidateView> filter_candidates_for_capability(
    const std::vector<AdapterCandidateView>& registered_candidates,
    const std::string& capability_id) {
  std::vector<AdapterCandidateView> capability_candidates;
  for (const auto& candidate : registered_candidates) {
    if (candidate_supports_capability(candidate, capability_id)) {
      capability_candidates.push_back(candidate);
    }
  }

  return capability_candidates;
}

[[nodiscard]] std::optional<AdapterRouteKind> preferred_locality_for_capability(
    const ServicePolicyView& policy_view,
    const std::vector<AdapterRouteKind>& route_classes) {
  for (const auto preferred_route_kind : policy_view.adapter_preference_order) {
    for (const auto supported_route_kind : route_classes) {
      if (supported_route_kind == preferred_route_kind) {
        return preferred_route_kind;
      }
    }
  }

  if (!route_classes.empty()) {
    return route_classes.front();
  }

  return std::nullopt;
}

[[nodiscard]] CapabilityRouteView build_live_route_view(
    const ServiceLiveCompositionOptions& options,
    const ServicePolicyView& policy_view,
    const std::vector<AdapterCandidateView>& registered_candidates,
    const std::string& capability_id,
    AdapterRouteRequestKind request_kind) {
  auto capability_candidates =
      filter_candidates_for_capability(registered_candidates, capability_id);
  std::vector<AdapterRouteKind> route_classes;
  for (const auto& candidate : capability_candidates) {
    append_route_kind_if_missing(&route_classes, candidate.route_kind);
  }

  std::vector<std::string> supported_actions;
  std::vector<std::string> supported_queries;
  if (request_kind == AdapterRouteRequestKind::action) {
    supported_actions.push_back(capability_id);
  } else if (capability_id != options.execution_capability_id ||
             capability_id == options.data_capability_id) {
    supported_queries = {"default", "catalog.list"};
  }

  return CapabilityRouteView{
      .capability_snapshot = CapabilitySnapshotView{
          .capability_id = capability_id,
          .capability_version = "v1",
          .supported_actions = std::move(supported_actions),
          .supported_queries = std::move(supported_queries),
          .route_classes = route_classes,
          .preferred_locality = preferred_locality_for_capability(policy_view,
                                                                  route_classes),
      },
      .registered_candidates = std::move(capability_candidates),
  };
}

[[nodiscard]] const ServiceLiveRouteBinding* find_binding_for(
    const ServiceLiveAdapterRegistry& registry,
    ServiceLiveRouteKind route_kind) {
  switch (route_kind) {
    case ServiceLiveRouteKind::local_platform:
      return registry.local_platform ? &*registry.local_platform : nullptr;
    case ServiceLiveRouteKind::local_service:
      return registry.local_service ? &*registry.local_service : nullptr;
    case ServiceLiveRouteKind::remote_service:
      return registry.remote_service ? &*registry.remote_service : nullptr;
  }

  return nullptr;
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
    sample.adapter_readiness = derive_adapter_readiness(policy_view_, registered_candidates_);
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

    policy_view_ = *policy_result.policy_view;
    const ServiceLiveAdapterRegistry effective_registry =
      options.adapter_registry.has_value()
        ? *options.adapter_registry
        : build_default_adapter_registry(options);
    registered_candidates_ = build_candidates(options, effective_registry);
    const auto route_equivalence_class = route_equivalence_class_for(effective_registry);
    const auto route_order = build_route_order(policy_view_, options);

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

    if (const auto* local_platform_binding =
        find_binding_for(effective_registry, ServiceLiveRouteKind::local_platform);
      local_platform_binding != nullptr) {
      local_platform_adapter_ = std::make_unique<LocalPlatformAdapter>(
        LocalPlatformAdapterOptions{
          .platform_hal_enabled = policy_view_.local_platform_route_enabled,
          .adapter_id = adapter_id_for(*local_platform_binding,
                         ServiceLiveRouteKind::local_platform),
          .invoke_platform = make_backend_handler(local_platform_binding->handler),
        });
    }

    if (const auto* local_service_binding =
        find_binding_for(effective_registry, ServiceLiveRouteKind::local_service);
      local_service_binding != nullptr) {
      local_service_adapter_ = std::make_unique<LocalServiceAdapter>(
        LocalServiceAdapterOptions{
          .service_endpoint_available =
            endpoint_available_for(local_service_binding->availability_state),
          .adapter_id = adapter_id_for(*local_service_binding,
                         ServiceLiveRouteKind::local_service),
          .invoke_service = make_backend_handler(local_service_binding->handler),
        });
    }

    if (const auto* remote_binding =
        find_binding_for(effective_registry, ServiceLiveRouteKind::remote_service);
      remote_binding != nullptr) {
      remote_service_adapter_ = std::make_unique<RemoteServiceAdapter>(
          RemoteServiceAdapterOptions{
          .remote_endpoint_available =
            endpoint_available_for(remote_binding->availability_state),
          .timeout_on_invoke = remote_binding->timeout_on_invoke,
          .adapter_id = adapter_id_for(*remote_binding,
                         ServiceLiveRouteKind::remote_service),
          .invoke_remote = make_backend_handler(remote_binding->handler),
          });
    }

    bridge_ = std::make_unique<AdapterBridge>(AdapterBridgeDependencies{
        .invokers = build_invokers(),
      .trace_bridge = trace_bridge_.get(),
    });

    projection_cache_ = std::make_unique<DataProjectionCache>(
        internal::DataProjectionCacheDependencies{
            .ttl_ms = policy_view_.data_cache_ttl_ms > 0
              ? static_cast<std::uint64_t>(policy_view_.data_cache_ttl_ms)
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
        .policy_view = policy_view_,
        .capability_snapshot = build_execution_snapshot(options, route_order),
        .fallback_envelope = build_fallback_envelope(
            "command.standard",
            route_order,
          options.allow_route_degrade,
          route_equivalence_class),
        .registered_candidates = registered_candidates_,
        .resolve_route_view = [this](const std::string& capability_id,
                                     AdapterRouteRequestKind request_kind) {
          return build_live_route_view(options_,
                                       policy_view_,
                                       registered_candidates_,
                                       capability_id,
                                       request_kind);
        },
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
        .policy_view = policy_view_,
        .capability_snapshot = build_data_snapshot(options, route_order),
        .fallback_envelope = build_fallback_envelope(
            "query.read_only",
            route_order,
          options.allow_route_degrade,
          route_equivalence_class),
        .registered_candidates = registered_candidates_,
        .resolve_route_view = [this](const std::string& capability_id,
                                     AdapterRouteRequestKind request_kind) {
          return build_live_route_view(options_,
                                       policy_view_,
                                       registered_candidates_,
                                       capability_id,
                                       request_kind);
        },
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
    if (local_platform_adapter_ != nullptr) {
      invokers.push_back(local_platform_adapter_.get());
    }
    if (local_service_adapter_ != nullptr) {
      invokers.push_back(local_service_adapter_.get());
    }
    if (remote_service_adapter_ != nullptr) {
      invokers.push_back(remote_service_adapter_.get());
    }
    return invokers;
  }

  internal::AdapterRouter router_;
  ResultMapper result_mapper_;
  internal::ServiceContextBuilder context_builder_;
  ServiceLiveCompositionOptions options_{};
  ServicePolicyView policy_view_{};
  std::vector<AdapterCandidateView> registered_candidates_;
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger_owner_;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider_owner_;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider_owner_;
  std::unique_ptr<ServiceAuditBridge> audit_bridge_;
  std::unique_ptr<ServiceMetricsBridge> metrics_bridge_;
  std::unique_ptr<ServiceTraceBridge> trace_bridge_;
  std::unique_ptr<LocalPlatformAdapter> local_platform_adapter_;
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
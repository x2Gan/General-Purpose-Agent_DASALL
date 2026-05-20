#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "IDataService.h"
#include "IExecutionService.h"

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::infra {

class IHealthProbe;

namespace audit {

class IAuditLogger;

}  // namespace audit

namespace metrics {

class IMetricsProvider;

}  // namespace metrics

namespace tracing {

class ITracerProvider;

}  // namespace tracing

}  // namespace dasall::infra

namespace dasall::services {

enum class ServiceLiveRequestKind {
  action,
  query,
};

enum class ServiceLiveRouteKind {
  local_platform,
  local_service,
  remote_service,
};

enum class ServiceLiveTrustClass {
  untrusted,
  caller_verified,
  trusted_local,
};

enum class ServiceLiveAvailabilityState {
  available,
  degraded,
  unavailable,
  unknown,
};

enum class ServiceLiveTransportOutcome {
  acknowledged,
  timeout,
  unreachable,
  rejected,
  partial,
};

struct ServiceLiveBackendRequest {
  std::string request_id;
  std::string capability_id;
  std::string target_id;
  ServiceLiveRequestKind request_kind = ServiceLiveRequestKind::action;
  std::string operation_name;
  std::string payload_json;
};

struct ServiceLiveBackendResult {
  ServiceLiveTransportOutcome transport_outcome =
      ServiceLiveTransportOutcome::rejected;
  std::string provider_status_code;
  std::string payload_json;
  std::uint32_t latency_ms = 0U;
  std::vector<std::string> side_effects;
  std::vector<std::string> evidence_refs;
};

using ServiceLiveBackendHandler = std::function<ServiceLiveBackendResult(
    const ServiceLiveBackendRequest& request)>;

struct ServiceLiveRouteBinding {
  std::string adapter_id;
  ServiceLiveTrustClass trust_class = ServiceLiveTrustClass::caller_verified;
  ServiceLiveAvailabilityState availability_state =
      ServiceLiveAvailabilityState::available;
  std::vector<std::string> supported_capabilities;
  ServiceLiveBackendHandler handler;
  bool timeout_on_invoke = false;
};

struct ServiceLiveAdapterRegistry {
  std::string route_equivalence_class = "service.live";
  std::optional<ServiceLiveRouteBinding> local_platform;
  std::optional<ServiceLiveRouteBinding> local_service;
  std::optional<ServiceLiveRouteBinding> remote_service;
};

struct ServiceLiveCompositionOptions {
  std::string execution_capability_id = "agent.terminal";
  std::string data_capability_id = "agent.dataset";
  bool local_service_available = true;
  bool remote_service_available = false;
  bool remote_timeout = false;
  bool allow_route_degrade = true;
  bool local_platform_route_enabled = false;
  bool observability_enabled = false;
  std::string observability_level = "minimal";
  std::string toolchain_hint = "x86_64-linux-gnu";
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider;
  bool health_probe_enabled = false;
  std::vector<std::string> critical_actions;
  std::vector<std::string> high_risk_actions = {"agent.terminal"};
  std::optional<ServiceLiveAdapterRegistry> adapter_registry;
};

struct ServiceLiveCompositionResult {
  std::shared_ptr<IExecutionService> execution_service;
  std::shared_ptr<IDataService> data_service;
  std::shared_ptr<infra::IHealthProbe> health_probe;
  std::string error;

  [[nodiscard]] bool ok() const {
    return execution_service != nullptr && data_service != nullptr && error.empty();
  }
};

[[nodiscard]] ServiceLiveCompositionResult compose_live_services(
    const profiles::RuntimePolicySnapshot& runtime_policy,
    const ServiceLiveCompositionOptions& options = {});

}  // namespace dasall::services
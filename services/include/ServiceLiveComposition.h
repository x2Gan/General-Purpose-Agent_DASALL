#pragma once

#include <memory>
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
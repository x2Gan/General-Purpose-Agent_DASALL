#pragma once

#include <memory>

namespace dasall::llm {

class ILLMManager;

}  // namespace dasall::llm

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::infra::logging {

class ILogger;

}  // namespace dasall::infra::logging

namespace dasall::infra::audit {

class IAuditLogger;

}  // namespace dasall::infra::audit

namespace dasall::infra::metrics {

class IMetricsProvider;

}  // namespace dasall::infra::metrics

namespace dasall::infra::tracing {

class ITracerProvider;

}  // namespace dasall::infra::tracing

namespace dasall::cognition {

struct CognitionRuntimeDependencies {
  std::shared_ptr<dasall::llm::ILLMManager> llm_manager;
  std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> policy_snapshot;
  std::shared_ptr<dasall::infra::logging::ILogger> logger;
  std::shared_ptr<dasall::infra::audit::IAuditLogger> audit_logger;
  std::shared_ptr<dasall::infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<dasall::infra::tracing::ITracerProvider> tracer_provider;
};

}  // namespace dasall::cognition

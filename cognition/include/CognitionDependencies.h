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

namespace dasall::cognition::observability {

class ICognitionTelemetrySink;

}  // namespace dasall::cognition::observability

namespace dasall::cognition {

struct CognitionRuntimeDependencies {
  std::shared_ptr<dasall::llm::ILLMManager> llm_manager = nullptr;
  std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> policy_snapshot = nullptr;
  std::shared_ptr<dasall::infra::logging::ILogger> logger = nullptr;
  std::shared_ptr<dasall::infra::audit::IAuditLogger> audit_logger = nullptr;
  std::shared_ptr<dasall::infra::metrics::IMetricsProvider> metrics_provider = nullptr;
  std::shared_ptr<dasall::infra::tracing::ITracerProvider> tracer_provider = nullptr;
  std::shared_ptr<dasall::cognition::observability::ICognitionTelemetrySink>
      telemetry_sink = nullptr;
};

}  // namespace dasall::cognition

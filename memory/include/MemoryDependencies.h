#pragma once

#include <memory>
#include <string>

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

namespace dasall::memory {

struct MemoryRuntimeDependencies {
  std::shared_ptr<dasall::infra::logging::ILogger> logger;
  std::shared_ptr<dasall::infra::audit::IAuditLogger> audit_logger;
  std::shared_ptr<dasall::infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<dasall::infra::tracing::ITracerProvider> tracer_provider;
  std::string profile_id = "unknown";
};

}  // namespace dasall::memory
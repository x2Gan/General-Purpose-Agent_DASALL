#pragma once

#include <memory>
#include <string>

#include "ILLMManager.h"

namespace dasall::infra::secret {

class ISecretBackend;

}  // namespace dasall::infra::secret

namespace dasall::infra::audit {

class IAuditLogger;

}  // namespace dasall::infra::audit

namespace dasall::infra::logging {

class ILogger;

}  // namespace dasall::infra::logging

namespace dasall::infra::metrics {

class IMetricsProvider;

}  // namespace dasall::infra::metrics

namespace dasall::infra::tracing {

class ITracerProvider;

}  // namespace dasall::infra::tracing

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::llm {

class ILLMTransport;

struct LLMProductionFactoryOptions {
  std::shared_ptr<infra::secret::ISecretBackend> secret_backend;
  std::shared_ptr<ILLMTransport> transport;
  std::string provider_catalog_baseline_root;
  std::shared_ptr<infra::logging::ILogger> logger;
  std::shared_ptr<infra::metrics::IMetricsProvider> metrics_provider;
  std::shared_ptr<infra::tracing::ITracerProvider> tracer_provider;
  std::shared_ptr<infra::audit::IAuditLogger> audit_logger;
};

struct LLMProductionFactoryResult {
  std::shared_ptr<ILLMManager> manager;
  std::string error;

  [[nodiscard]] bool ok() const {
    return manager != nullptr && error.empty();
  }
};

[[nodiscard]] LLMProductionFactoryResult create_production_llm_manager(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    LLMProductionFactoryOptions options = {});

}  // namespace dasall::llm
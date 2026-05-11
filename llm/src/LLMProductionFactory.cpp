#include "LLMProductionFactory.h"

#include <memory>
#include <filesystem>
#include <string>
#include <utility>

#include "LLMManager.h"
#include "LLMSubsystemConfig.h"
#include "UsageAggregator.h"
#include "adapters/OpenAICompatibleAdapter.h"
#include "config/InstallLayout.h"
#include "execution/ResponseNormalizer.h"
#include "prompt/PromptPipeline.h"
#include "provider/ProviderCatalogRepository.h"
#include "route/AdapterRegistry.h"
#include "route/ModelRouter.h"
#include "secret/backends/FileSecretBackend.h"
#include "transport/CurlCommandLLMTransport.h"

namespace dasall::llm {
namespace {

constexpr std::string_view kOpenAICompatibleFamily = "openai_compatible";

[[nodiscard]] std::shared_ptr<infra::secret::ISecretBackend> make_default_secret_backend() {
  const auto layout = infra::config::resolve_install_layout();
  return std::make_shared<infra::secret::FileSecretBackend>(
      infra::secret::FileSecretBackendOptions{
          .root_dir = layout.state_root / "secrets",
          .encrypt_at_rest = true,
          .backend_ref = "file.primary",
          .lease_duration_ms = 60000,
          .rotation_epoch = 1,
      });
}

[[nodiscard]] LLMProductionFactoryResult make_failure(std::string error) {
  return LLMProductionFactoryResult{
      .manager = nullptr,
      .error = std::move(error),
  };
}

}  // namespace

LLMProductionFactoryResult create_production_llm_manager(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    LLMProductionFactoryOptions options) {
  const auto config = project_llm_subsystem_config(policy_snapshot);
  if (!config.has_value()) {
    return make_failure("production llm config projection failed");
  }

  auto provider_repository = std::make_shared<provider::ProviderCatalogRepository>();
  if (!provider_repository->init(config->provider_catalog_sources) ||
      !provider_repository->reload()) {
    return make_failure("production provider catalog load failed: " +
                        provider_repository->last_error_message());
  }

  auto provider_snapshot = provider_repository->snapshot();
  if (provider_snapshot == nullptr || !provider_snapshot->has_consistent_values()) {
    return make_failure("production provider catalog snapshot is invalid");
  }

  auto registry = std::make_shared<route::AdapterRegistry>();
  if (!registry->init(route::AdapterRegistryConfig{
          .blocked_failure_threshold = config->timeout_policy.circuit_breaker_threshold,
      })) {
    return make_failure("production adapter registry init failed: " +
                        registry->last_error_message());
  }

  if (options.secret_backend == nullptr) {
    options.secret_backend = make_default_secret_backend();
  }

  auto transport = std::make_shared<transport::CurlCommandLLMTransport>(
      transport::CurlCommandLLMTransportOptions{
          .curl_path = "/usr/bin/curl",
          .temp_dir = std::filesystem::temp_directory_path(),
          .secret_backend = options.secret_backend,
          .actor = "dasall-daemon",
      });

  std::size_t registered_routes = 0U;
  for (const auto& provider : provider_snapshot->providers) {
    if (provider.descriptor.adapter_family != kOpenAICompatibleFamily) {
      continue;
    }

    const ProviderRuntimeProjectionView runtime_view{
        .provider_instance_id = provider.descriptor.provider_id,
        .base_url_alias = provider.runtime.base_url_alias,
        .snapshot_version = provider.descriptor.source_version,
        .runtime_tags = provider.runtime.tags,
        .activation_flag = provider.runtime.activation_flag,
    };

    for (const auto& model : provider_snapshot->models) {
      if (model.summary.provider_id != provider.descriptor.provider_id) {
        continue;
      }

      auto adapter = std::make_shared<OpenAICompatibleAdapter>(transport);
      if (!registry->initialize_and_register_provider_route(provider.descriptor,
                                                           runtime_view,
                                                           model.summary.model_id,
                                                           model.supports_streaming,
                                                           *config,
                                                           adapter)) {
        return make_failure("production adapter route registration failed: " +
                            registry->last_error_message());
      }
      ++registered_routes;
    }
  }

  if (registered_routes == 0U) {
    return make_failure("production adapter registry has no openai-compatible routes");
  }

  auto manager = std::make_shared<LLMManager>(
      std::make_shared<prompt::PromptPipeline>(),
      std::make_shared<route::ModelRouter>(),
      registry,
      std::make_shared<LLMCallExecutor>(),
      std::make_shared<execution::ResponseNormalizer>(),
      std::make_shared<UsageAggregator>(),
      provider_snapshot);
  if (!manager->init(*config)) {
    return make_failure("production llm manager init failed");
  }

  return LLMProductionFactoryResult{
      .manager = manager,
      .error = {},
  };
}

}  // namespace dasall::llm
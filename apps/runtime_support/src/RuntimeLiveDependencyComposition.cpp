#include "RuntimeLiveDependencyComposition.h"

#include <memory>
#include <string>
#include <utility>

#include "ICognitionEngine.h"
#include "LLMProductionFactory.h"
#include "IMemoryManager.h"
#include "KnowledgeServiceFactory.h"
#include "IResponseBuilder.h"
#include "RuntimeDependencySet.h"
#include "ToolManager.h"
#include "config/InstallLayout.h"

namespace dasall::apps::runtime_support {

RuntimeDependencyCompositionResult compose_minimal_live_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const std::string_view& composition_owner) {
  if (policy_snapshot == nullptr) {
    return RuntimeDependencyCompositionResult{
                .dependency_set = nullptr,
        .error = "runtime policy snapshot is required for live dependency composition",
    };
  }

  auto dependency_set = std::make_shared<runtime::RuntimeDependencySet>();

  memory::MemoryConfig memory_config;
  memory_config.storage.backend = memory::StorageBackend::Memory;

  auto memory_manager =
      std::shared_ptr<memory::IMemoryManager>(memory::create_memory_manager(memory_config));
  if (memory_manager == nullptr) {
    return RuntimeDependencyCompositionResult{
                .dependency_set = nullptr,
        .error = std::string("memory manager factory returned null for ") +
                 std::string(composition_owner),
    };
  }

  const auto init_code = memory_manager->init(memory_config);
  if (static_cast<int>(init_code) != 0) {
    return RuntimeDependencyCompositionResult{
                .dependency_set = nullptr,
        .error = std::string("memory manager init failed for ") +
                 std::string(composition_owner),
    };
  }
  dependency_set->memory_manager = std::move(memory_manager);

  auto llm_result = llm::create_production_llm_manager(*policy_snapshot);
  if (!llm_result.ok()) {
    return RuntimeDependencyCompositionResult{
                .dependency_set = nullptr,
        .error = std::string("llm manager composition failed for ") +
                 std::string(composition_owner) + ": " + llm_result.error,
    };
  }
  dependency_set->llm_manager = std::move(llm_result.manager);

  auto cognition_engine = cognition::create_cognition_engine(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
          .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
      });
  if (!cognition_engine) {
    return RuntimeDependencyCompositionResult{
                .dependency_set = nullptr,
        .error = std::string("cognition engine composition failed for ") +
                 std::string(composition_owner),
    };
  }
  dependency_set->cognition_engine =
      std::shared_ptr<cognition::ICognitionEngine>(cognition_engine.release());

  auto response_builder = cognition::create_response_builder(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
        .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
      });
  if (!response_builder) {
    return RuntimeDependencyCompositionResult{
                .dependency_set = nullptr,
        .error = std::string("response builder composition failed for ") +
                 std::string(composition_owner),
    };
  }
  dependency_set->response_builder =
      std::shared_ptr<cognition::IResponseBuilder>(response_builder.release());

  dependency_set->tool_manager = std::make_shared<dasall::tools::ToolManager>();
  dependency_set->visible_tools = {"agent.dataset"};
  dependency_set->external_evidence = {
      std::string("runtime:") + std::string(composition_owner) +
      ":required-live-baseline",
  };

    const auto install_layout = infra::config::resolve_install_layout();
    const auto knowledge_result = knowledge::create_installed_asset_knowledge_service(
      knowledge::InstalledAssetKnowledgeServiceOptions{
        .readonly_assets_root = install_layout.readonly_assets_root,
        .state_root = install_layout.state_root,
        .service_instance_id = std::string(composition_owner) + ":knowledge",
      });
    if (knowledge_result.ok()) {
    dependency_set->knowledge_service = knowledge_result.service;
    dependency_set->external_evidence.push_back(
      std::string("runtime:") + std::string(composition_owner) +
      ":knowledge-installed-assets");
    } else {
    dependency_set->external_evidence.push_back(
      std::string("runtime:") + std::string(composition_owner) +
      ":knowledge-unavailable:" + knowledge_result.error);
    }

  return RuntimeDependencyCompositionResult{
      .dependency_set = std::move(dependency_set),
      .error = {},
  };
}

}  // namespace dasall::apps::runtime_support
#include "RuntimeLiveDependencyComposition.h"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
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
namespace {

namespace fs = std::filesystem;

[[nodiscard]] RuntimeDependencyCompositionResult make_error(std::string error) {
  return RuntimeDependencyCompositionResult{
      .dependency_set = nullptr,
      .error = std::move(error),
  };
}

[[nodiscard]] fs::path selected_root(const fs::path& default_root,
                                     const fs::path& override_root) {
  return override_root.empty() ? default_root : override_root;
}

[[nodiscard]] std::string create_memory_state_dir(const fs::path& state_root,
                                                  const std::string_view& composition_owner) {
  const fs::path memory_state_root = state_root / "memory";
  std::error_code error;
  fs::create_directories(memory_state_root, error);
  if (error) {
    return std::string("memory state directory unavailable for ") +
           std::string(composition_owner) + ": " + memory_state_root.string() +
           ": " + error.message();
  }
  return {};
}

[[nodiscard]] memory::MemoryConfig make_sqlite_memory_config(
    const fs::path& readonly_assets_root,
    const fs::path& state_root) {
  memory::MemoryConfig memory_config;
  memory_config.storage.backend = memory::StorageBackend::Sqlite;
  memory_config.storage.db_path = (state_root / "memory" / "memory.db").string();
  memory_config.storage.migrations_dir =
      (readonly_assets_root / "sql" / "memory").string();
  memory_config.vector.enabled = false;
  return memory_config;
}

}  // namespace

RuntimeDependencyCompositionResult compose_minimal_live_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const std::string_view& composition_owner,
    const RuntimeLiveDependencyCompositionOptions& options) {
  if (policy_snapshot == nullptr) {
    return make_error("runtime policy snapshot is required for live dependency composition");
  }

  const auto install_layout = infra::config::resolve_install_layout();
  const fs::path readonly_assets_root = selected_root(
      install_layout.readonly_assets_root, options.readonly_assets_root_override);
  const fs::path state_root = selected_root(
      install_layout.state_root, options.state_root_override);
  if (const auto state_error = create_memory_state_dir(state_root, composition_owner);
      !state_error.empty()) {
    return make_error(state_error);
  }

  auto dependency_set = std::make_shared<runtime::RuntimeDependencySet>();

  memory::MemoryConfig memory_config = make_sqlite_memory_config(
      readonly_assets_root, state_root);

  auto memory_manager =
      std::shared_ptr<memory::IMemoryManager>(memory::create_memory_manager(memory_config));
  if (memory_manager == nullptr) {
    return make_error(std::string("memory manager factory returned null for ") +
                      std::string(composition_owner));
  }

  const auto init_code = memory_manager->init(memory_config);
  if (static_cast<int>(init_code) != 0) {
    return make_error(std::string("memory manager init failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->memory_manager = std::move(memory_manager);

  auto llm_result = llm::create_production_llm_manager(*policy_snapshot);
  if (!llm_result.ok()) {
    return make_error(std::string("llm manager composition failed for ") +
                      std::string(composition_owner) + ": " + llm_result.error);
  }
  dependency_set->llm_manager = std::move(llm_result.manager);

  auto cognition_engine = cognition::create_cognition_engine(
      *policy_snapshot,
      cognition::CognitionRuntimeDependencies{
          .llm_manager = dependency_set->llm_manager,
          .policy_snapshot = policy_snapshot,
      });
  if (!cognition_engine) {
    return make_error(std::string("cognition engine composition failed for ") +
                      std::string(composition_owner));
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
    return make_error(std::string("response builder composition failed for ") +
                      std::string(composition_owner));
  }
  dependency_set->response_builder =
      std::shared_ptr<cognition::IResponseBuilder>(response_builder.release());

  dependency_set->tool_manager = std::make_shared<dasall::tools::ToolManager>();
  dependency_set->visible_tools = {"agent.dataset"};
  dependency_set->external_evidence = {
      std::string("runtime:") + std::string(composition_owner) +
      ":required-live-baseline",
  };

  const auto knowledge_result = knowledge::create_installed_asset_knowledge_service(
      knowledge::InstalledAssetKnowledgeServiceOptions{
          .readonly_assets_root = readonly_assets_root,
          .state_root = state_root,
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
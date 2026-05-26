#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::runtime {

class RuntimeDependencySet;

}  // namespace dasall::runtime

namespace dasall::platform {

class ITimer;

}  // namespace dasall::platform

namespace dasall::knowledge {

struct DenseStoreFactoryContext;

namespace index {

struct DenseSnapshotBuildRequest;
struct DenseSnapshotBuildResult;

}  // namespace index

namespace retrieve {

class IQueryEncoder;
class IVectorRecallStore;

}  // namespace retrieve

}  // namespace dasall::knowledge

namespace dasall::apps::runtime_support {

enum class RuntimeKnowledgeRefreshPlanKind : std::uint8_t {
  Skip = 0,
  Selective = 1,
  FullScanFallback = 2,
};

struct RuntimeKnowledgeRefreshPlan {
  RuntimeKnowledgeRefreshPlanKind kind =
      RuntimeKnowledgeRefreshPlanKind::FullScanFallback;
  std::vector<std::string> added_sources;
  std::vector<std::string> updated_sources;
  std::vector<std::string> removed_sources;
};

class IRuntimeKnowledgeRefreshSourceProvider {
 public:
  virtual ~IRuntimeKnowledgeRefreshSourceProvider() = default;

  [[nodiscard]] virtual RuntimeKnowledgeRefreshPlan next_plan() = 0;
};

struct RuntimeDependencyCompositionResult {
  std::shared_ptr<runtime::RuntimeDependencySet> dependency_set;
  std::string error;

  [[nodiscard]] bool ok() const {
    return dependency_set != nullptr && error.empty();
  }
};

struct RuntimeLiveDependencyCompositionOptions {
  std::filesystem::path readonly_assets_root_override;
  std::filesystem::path runtime_library_root_override;
  std::filesystem::path state_root_override;
  std::function<knowledge::index::DenseSnapshotBuildResult(
      const knowledge::index::DenseSnapshotBuildRequest& request)>
      build_dense_snapshot_override;
  std::function<std::unique_ptr<knowledge::retrieve::IVectorRecallStore>(
      const knowledge::DenseStoreFactoryContext& context)>
      create_vector_recall_store_override;
  std::function<std::unique_ptr<knowledge::retrieve::IQueryEncoder>()>
      create_query_encoder_override;
  std::shared_ptr<platform::ITimer> knowledge_refresh_timer;
  std::shared_ptr<IRuntimeKnowledgeRefreshSourceProvider>
      knowledge_refresh_source_provider;
};

[[nodiscard]] RuntimeDependencyCompositionResult compose_minimal_live_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const std::string_view& composition_owner,
    const RuntimeLiveDependencyCompositionOptions& options = {});

}  // namespace dasall::apps::runtime_support
#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::runtime {

class RuntimeDependencySet;

}  // namespace dasall::runtime

namespace dasall::apps::runtime_support {

struct RuntimeDependencyCompositionResult {
  std::shared_ptr<runtime::RuntimeDependencySet> dependency_set;
  std::string error;

  [[nodiscard]] bool ok() const {
    return dependency_set != nullptr && error.empty();
  }
};

[[nodiscard]] RuntimeDependencyCompositionResult compose_minimal_live_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const std::string_view& composition_owner);

}  // namespace dasall::apps::runtime_support
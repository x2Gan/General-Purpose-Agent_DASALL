#pragma once

#include <memory>

namespace dasall::llm {

class ILLMManager;

}  // namespace dasall::llm

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::cognition {

struct CognitionRuntimeDependencies {
  std::shared_ptr<dasall::llm::ILLMManager> llm_manager;
  std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> policy_snapshot;
};

}  // namespace dasall::cognition

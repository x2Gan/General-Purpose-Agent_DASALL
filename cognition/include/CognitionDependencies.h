#pragma once

#include <memory>

namespace dasall::llm {

class ILLMManager;

}  // namespace dasall::llm

namespace dasall::cognition {

struct CognitionRuntimeDependencies {
  std::shared_ptr<dasall::llm::ILLMManager> llm_manager;
};

}  // namespace dasall::cognition

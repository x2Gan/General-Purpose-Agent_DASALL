#pragma once

#include <memory>
#include <string>

#include "llm/LLMRequest.h"

namespace dasall::llm {

struct ModelSelectionHint;

struct LLMGenerateRequest {
  std::string stage;
  std::string task_type;

  // model_route may still be an unresolved pre-route hint here; LLMManager is
  // responsible for resolving the final adapter route before provider dispatch.
  contracts::LLMRequest request;

  std::shared_ptr<const ModelSelectionHint> selection_hint;
};

}  // namespace dasall::llm

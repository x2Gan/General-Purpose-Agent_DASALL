#pragma once

#include <memory>
#include <optional>
#include <string>

#include "llm/LLMRequest.h"

namespace dasall::llm {

struct ModelSelectionHint;

struct LLMGenerateRequest {
  std::string stage;
  std::string task_type;

  // request.model_route is a required pre-route hint. LLMManager resolves the
  // final adapter route before provider dispatch and overwrites the adapter
  // request with the concrete route key.
  contracts::LLMRequest request;

  // An explicit registry selector that must stay independent from
  // request.prompt_id/request.prompt_version, which remain response audit
  // anchors after PromptPipeline selects a release.
  std::optional<std::string> prompt_release_id_override;

  std::shared_ptr<const ModelSelectionHint> selection_hint;
};

}  // namespace dasall::llm

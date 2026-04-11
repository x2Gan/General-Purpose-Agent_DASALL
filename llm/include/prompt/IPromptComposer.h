#pragma once

#include "prompt/ModelBudgetHint.h"
#include "prompt/PromptComposeRequest.h"
#include "prompt/PromptComposeResult.h"
#include "prompt/PromptComposerConfig.h"
#include "prompt/PromptRelease.h"

namespace dasall::llm::prompt {

class IPromptComposer {
 public:
  virtual ~IPromptComposer() = default;

  virtual bool init(const PromptComposerConfig& config) = 0;
  virtual dasall::contracts::PromptComposeResult compose(
      const dasall::contracts::PromptComposeRequest& request,
      const dasall::contracts::PromptRelease& release,
      const ModelBudgetHint& budget_hint) const = 0;
};

}  // namespace dasall::llm::prompt
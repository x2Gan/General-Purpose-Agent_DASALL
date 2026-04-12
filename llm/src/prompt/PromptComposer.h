#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "TemplateRenderer.h"
#include "prompt/IPromptComposer.h"

#include "../TokenEstimator.h"

namespace dasall::llm::prompt {

using FewShotResolver = std::function<std::vector<std::string>(
    const dasall::contracts::PromptRelease& release,
    std::uint32_t max_few_shot_count,
    std::vector<std::string>& warnings)>;

class PromptComposer final : public IPromptComposer {
 public:
  explicit PromptComposer(
      std::shared_ptr<ITemplateRenderer> renderer = std::make_shared<TemplateRenderer>(),
      std::shared_ptr<dasall::llm::TokenEstimator> token_estimator =
          std::make_shared<dasall::llm::TokenEstimator>(),
      FewShotResolver few_shot_resolver = {});

  bool init(const PromptComposerConfig& config) override;
  [[nodiscard]] dasall::contracts::PromptComposeResult compose(
      const dasall::contracts::PromptComposeRequest& request,
      const dasall::contracts::PromptRelease& release,
      const ModelBudgetHint& budget_hint) const override;

 private:
  [[nodiscard]] std::vector<std::string> resolve_few_shots(
      const dasall::contracts::PromptRelease& release,
      std::vector<std::string>& warnings) const;

  std::shared_ptr<ITemplateRenderer> renderer_;
  std::shared_ptr<dasall::llm::TokenEstimator> token_estimator_;
  FewShotResolver few_shot_resolver_;
  PromptComposerConfig config_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::prompt
#pragma once

#include <memory>

#include "prompt/IPromptPolicy.h"

#include "../TokenEstimator.h"

namespace dasall::llm::prompt {

class PromptPolicy final : public IPromptPolicy {
 public:
  explicit PromptPolicy(
      std::shared_ptr<dasall::llm::TokenEstimator> token_estimator =
          std::make_shared<dasall::llm::TokenEstimator>());

  bool init(const PromptPolicyConfig& config) override;
  [[nodiscard]] PromptPolicyDecision evaluate(
      const dasall::contracts::PromptComposeResult& compose_result,
      const PromptPolicyInput& input) const override;

 private:
  std::shared_ptr<dasall::llm::TokenEstimator> token_estimator_;
  PromptPolicyConfig config_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::prompt
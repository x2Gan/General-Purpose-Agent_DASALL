#pragma once

#include <memory>

#include "PromptComposer.h"
#include "PromptPolicy.h"
#include "PromptRegistry.h"
#include "prompt/IPromptPipeline.h"

namespace dasall::llm::prompt {

class PromptPipeline final : public IPromptPipeline {
 public:
  explicit PromptPipeline(
      std::shared_ptr<IPromptRegistry> registry = std::make_shared<PromptRegistry>(),
      std::shared_ptr<IPromptComposer> composer = std::make_shared<PromptComposer>(),
      std::shared_ptr<IPromptPolicy> policy = std::make_shared<PromptPolicy>());

  bool init(const PromptPipelineConfig& config) override;
  [[nodiscard]] PromptPipelineResult run(
      const PromptQuery& query,
      const dasall::contracts::PromptComposeRequest& compose_request,
      const PromptPolicyInput& policy_input) const override;
  [[nodiscard]] std::optional<PromptAssetMetadata> lookup_release_asset(
      std::string_view prompt_release_id) const override;

 private:
  std::shared_ptr<IPromptRegistry> registry_;
  std::shared_ptr<IPromptComposer> composer_;
  std::shared_ptr<IPromptPolicy> policy_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::prompt
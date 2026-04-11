#pragma once

#include "prompt/PromptComposerConfig.h"
#include "prompt/PromptPolicyConfig.h"
#include "prompt/PromptRegistryConfig.h"

namespace dasall::llm::prompt {

struct PromptPipelineConfig {
  PromptRegistryConfig registry_config;
  PromptComposerConfig composer_config;
  PromptPolicyConfig policy_config;
};

}  // namespace dasall::llm::prompt

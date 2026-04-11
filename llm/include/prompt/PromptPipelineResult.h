#pragma once

#include <optional>
#include <string>

#include "prompt/PromptComposeResult.h"
#include "prompt/PromptPolicyDecision.h"
#include "prompt/PromptRegistryResult.h"

namespace dasall::llm::prompt {

struct PromptPipelineResult {
  PromptPolicyDisposition disposition = PromptPolicyDisposition::Deny;
  std::optional<dasall::contracts::PromptComposeResult> compose_result;
  std::optional<PromptPolicyDecision> policy_decision;
  std::optional<PromptRegistryResult> registry_result;
  std::string reason;
};

}  // namespace dasall::llm::prompt

#pragma once

#include <string>
#include <vector>

namespace dasall::llm::prompt {

enum class PromptPolicyDisposition {
  Allow = 0,
  Deny = 1,
  OverBudget = 2,
  RequireRecompose = 3,
};

struct PromptPolicyDecision {
  PromptPolicyDisposition disposition = PromptPolicyDisposition::Deny;
  std::vector<std::string> governed_messages;
  std::vector<std::string> redactions;
  std::vector<std::string> tool_visibility_patch;
  std::string reason;

  [[nodiscard]] bool has_consistent_values() const {
    if (reason.empty()) {
      return false;
    }

    const bool has_governed_messages = !governed_messages.empty();

    switch (disposition) {
      case PromptPolicyDisposition::Allow:
        return has_governed_messages;
      case PromptPolicyDisposition::Deny:
      case PromptPolicyDisposition::OverBudget:
      case PromptPolicyDisposition::RequireRecompose:
        return !has_governed_messages;
    }

    return false;
  }
};

}  // namespace dasall::llm::prompt
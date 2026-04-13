#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMResponse.h"
#include "prompt/PromptPolicyDecision.h"

namespace dasall::llm {

enum class LLMFailureCategory {
  PromptAsset = 0,
  PromptGovernance = 1,
  Routing = 2,
  AdapterTransport = 3,
  ProviderProtocol = 4,
  FallbackExhausted = 5,
};

struct LLMManagerResult {
  std::optional<contracts::ResultCode> code;
  std::optional<contracts::LLMResponse> response;
  std::optional<contracts::ErrorInfo> error;
  std::string resolved_route;
  std::vector<std::string> attempted_routes;
  std::optional<LLMFailureCategory> failure_category;
  std::optional<prompt::PromptPolicyDisposition> governance_disposition;
  bool fallback_used = false;

  [[nodiscard]] bool has_consistent_values() const {
    const bool has_response = response.has_value();
    const bool has_failure = error.has_value();

    if (has_response == has_failure) {
      return false;
    }

    if (has_failure != code.has_value()) {
      return false;
    }

    if (has_failure != failure_category.has_value()) {
      return false;
    }

    if (has_response && governance_disposition.has_value()) {
      return false;
    }

    if (governance_disposition.has_value()) {
      if (!has_failure || !failure_category.has_value() ||
          *failure_category != LLMFailureCategory::PromptGovernance) {
        return false;
      }
    }

    if (failure_category.has_value() &&
        *failure_category == LLMFailureCategory::PromptGovernance &&
        !governance_disposition.has_value()) {
      return false;
    }

    if (has_response && resolved_route.empty()) {
      return false;
    }

    if (!attempted_routes.empty()) {
      if (resolved_route.empty()) {
        return false;
      }

      if (attempted_routes.back() != resolved_route) {
        return false;
      }
    }

    if (fallback_used && attempted_routes.size() < 2U) {
      return false;
    }

    if (failure_category.has_value() &&
        *failure_category == LLMFailureCategory::FallbackExhausted &&
        attempted_routes.size() < 2U) {
      return false;
    }

    if (has_failure && code.has_value() && error->failure_type.has_value() &&
        contracts::classify_result_code(*code) != *error->failure_type) {
      return false;
    }

    if (governance_disposition.has_value() && error.has_value() &&
        (*governance_disposition == prompt::PromptPolicyDisposition::OverBudget ||
         *governance_disposition == prompt::PromptPolicyDisposition::RequireRecompose) &&
        !error->safe_to_replan.value_or(false)) {
      return false;
    }

    return true;
  }
};

}  // namespace dasall::llm

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMResponse.h"

namespace dasall::llm {

struct AdapterUsageFragment {
  std::optional<std::uint32_t> prompt_tokens;
  std::optional<std::uint32_t> completion_tokens;
  std::optional<std::uint32_t> total_tokens;
  std::optional<std::uint32_t> prompt_cache_hit_tokens;
  std::optional<std::uint32_t> prompt_cache_miss_tokens;

  [[nodiscard]] bool has_consistent_values() const {
    const bool has_prompt_tokens = prompt_tokens.has_value();
    const bool has_completion_tokens = completion_tokens.has_value();
    const bool has_total_tokens = total_tokens.has_value();
    if (has_prompt_tokens || has_completion_tokens || has_total_tokens) {
      if (!(has_prompt_tokens && has_completion_tokens && has_total_tokens)) {
        return false;
      }

      if (*total_tokens != *prompt_tokens + *completion_tokens) {
        return false;
      }
    }

    if ((prompt_cache_hit_tokens.has_value() || prompt_cache_miss_tokens.has_value()) &&
        !has_prompt_tokens) {
      return false;
    }

    if (has_prompt_tokens && prompt_cache_hit_tokens.has_value() &&
        prompt_cache_miss_tokens.has_value() &&
        *prompt_cache_hit_tokens + *prompt_cache_miss_tokens > *prompt_tokens) {
      return false;
    }

    return true;
  }

  [[nodiscard]] bool has_token_counts() const {
    return prompt_tokens.has_value() && completion_tokens.has_value() &&
           total_tokens.has_value();
  }
};

struct AdapterProviderDiagnostics {
  std::string reasoning_content;
  std::string provider_trace_id;
  std::vector<std::string> audit_tags;

  [[nodiscard]] bool has_consistent_values() const {
    std::unordered_set<std::string> unique_tags;
    unique_tags.reserve(audit_tags.size());
    return std::all_of(audit_tags.begin(), audit_tags.end(), [&](const std::string& tag) {
      return !tag.empty() && unique_tags.insert(tag).second;
    });
  }
};

// AdapterCallResult keeps provider transport/protocol failure facts inside llm
// and avoids exception-based error propagation across the adapter SPI.
struct AdapterCallResult {
  std::optional<contracts::LLMResponse> response;
  std::optional<contracts::ErrorInfo> error;
  std::optional<contracts::ResultCode> result_code;
  std::optional<AdapterUsageFragment> usage;
  AdapterProviderDiagnostics provider_diagnostics;

  [[nodiscard]] bool has_consistent_values() const {
    if (response.has_value() == error.has_value()) {
      return false;
    }

    if (error.has_value() != result_code.has_value()) {
      return false;
    }

    if (usage.has_value()) {
      if (!response.has_value() || !usage->has_consistent_values()) {
        return false;
      }
    }

    if (!provider_diagnostics.has_consistent_values()) {
      return false;
    }

    return true;
  }
};

}  // namespace dasall::llm

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "prompt/PromptRelease.h"

#include "../adapters/AdapterCallResult.h"

namespace dasall::llm::execution {

struct ResponseNormalizerContext {
  std::string route_key;
  std::string provider_id;
  std::string model_id;
  std::string model_name;
  std::string prompt_id;
  std::string prompt_version;
  std::optional<dasall::contracts::PromptEvalStatus> prompt_eval_status;
  std::string prompt_release_scope;
  std::optional<std::string> request_id;
  std::optional<std::string> llm_call_id;
  std::int64_t completed_at_ms = 0;

  [[nodiscard]] bool has_consistent_values() const;
};

struct ResponseNormalizationResult {
  std::optional<contracts::LLMResponse> response;
  std::optional<contracts::ErrorInfo> error;
  std::optional<contracts::ResultCode> result_code;
  std::optional<AdapterUsageFragment> usage_fragment;
  std::vector<std::string> audit_events;
  std::string provider_trace_id;
  bool reasoning_content_stripped = false;

  [[nodiscard]] bool succeeded() const;
  [[nodiscard]] bool has_consistent_values() const;
};

class ResponseNormalizer {
 public:
  [[nodiscard]] ResponseNormalizationResult normalize(
      const AdapterCallResult& adapter_result,
      const ResponseNormalizerContext& context) const;
};

}  // namespace dasall::llm::execution
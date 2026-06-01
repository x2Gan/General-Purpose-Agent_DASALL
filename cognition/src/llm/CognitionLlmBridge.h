#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CognitionTypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "ILLMManager.h"
#include "LLMGenerateRequest.h"
#include "LLMManagerResult.h"
#include "llm/LLMResponse.h"

namespace dasall::cognition::llm_bridge {

enum class StageSchemaKind : std::uint8_t {
  Text = 0,
  JsonObject = 1,
  JsonSchema = 2,
};

struct StageSchemaSpec {
  StageSchemaKind schema_kind = StageSchemaKind::Text;
  std::string output_schema_ref;
  bool allow_plain_text_fallback = false;
};

struct StageBudgetHint {
  std::uint32_t estimated_input_tokens = 0;
  std::uint32_t target_output_tokens = 0;
  std::uint32_t remaining_tokens = 0;
  bool near_budget_limit = false;
  std::string budget_tier = "unspecified";
};

struct StageLlmCallRequest {
  std::string request_id;
  std::string trace_id;
  std::string llm_call_id;
  std::string stage_name;
  std::string task_type;
  std::vector<std::string> messages;
  StageModelHint model_hint;
  std::optional<BudgetContext> budget_context;
  StageSchemaSpec schema_spec;
  bool prefer_streaming = false;
};

struct StageLlmCallResult {
  std::optional<contracts::LLMResponse> response;
  std::optional<contracts::ErrorInfo> error_info;
  std::optional<contracts::ResultCode> result_code;
  std::optional<std::uint32_t> prompt_tokens;
  std::optional<std::uint32_t> completion_tokens;
  std::optional<double> total_cost;
  std::optional<std::string> finish_reason;
  StageBudgetHint budget_hint;
  std::string resolved_route;
  std::vector<std::string> warnings;
  std::vector<std::string> diagnostics;
  bool fallback_used = false;
};

struct LlmFailureProjection {
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  contracts::ErrorInfo error_info;
  std::string diagnostic;
};

class CognitionLlmBridge {
 public:
  explicit CognitionLlmBridge(std::shared_ptr<dasall::llm::ILLMManager> llm_manager);

  [[nodiscard]] StageLlmCallResult invoke_stage(const StageLlmCallRequest& request) const;
  [[nodiscard]] bool abandon_call(std::string_view llm_call_id) const;
  [[nodiscard]] dasall::llm::LLMGenerateRequest build_llm_request(
      const StageLlmCallRequest& request) const;
  [[nodiscard]] StageBudgetHint derive_budget_hint(const StageLlmCallRequest& request) const;
  [[nodiscard]] StageLlmCallResult normalize_llm_response(
      const StageLlmCallRequest& request,
      const dasall::llm::LLMManagerResult& manager_result) const;
  [[nodiscard]] LlmFailureProjection project_llm_failure(
      const StageLlmCallRequest& request,
      const dasall::llm::LLMManagerResult& manager_result) const;

 private:
  std::shared_ptr<dasall::llm::ILLMManager> llm_manager_;
};

}  // namespace dasall::cognition::llm_bridge
#include "llm/CognitionLlmBridge.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ResultCode.h"
#include "route/ModelSelectionHint.h"

namespace dasall::cognition::llm_bridge {
namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::string resolve_stage_name(const StageLlmCallRequest& request) {
  if (!request.stage_name.empty()) {
    return request.stage_name;
  }

  return request.model_hint.stage_name;
}

[[nodiscard]] std::string resolve_task_type(const StageLlmCallRequest& request) {
  if (!request.task_type.empty()) {
    return request.task_type;
  }

  return request.model_hint.task_type;
}

[[nodiscard]] bool requires_structured_output(const StageLlmCallRequest& request) {
  return request.model_hint.requires_structured_output ||
         request.schema_spec.schema_kind != StageSchemaKind::Text;
}

[[nodiscard]] std::string complexity_tier_name(const ModelCapabilityTier tier) {
  switch (tier) {
    case ModelCapabilityTier::Lightweight:
      return "lightweight";
    case ModelCapabilityTier::Standard:
      return "standard";
    case ModelCapabilityTier::Advanced:
      return "advanced";
    case ModelCapabilityTier::ReasoningHeavy:
      return "reasoning_heavy";
  }

  return "standard";
}

[[nodiscard]] std::string latency_sla_tier(const std::uint32_t deadline_ms) {
  if (deadline_ms > 0U && deadline_ms <= 1000U) {
    return "low_latency";
  }

  if (deadline_ms > 0U && deadline_ms <= 2500U) {
    return "interactive";
  }

  return "background";
}

[[nodiscard]] std::uint32_t estimate_input_tokens(const std::vector<std::string>& messages) {
  std::size_t total_chars = 0U;
  for (const auto& message : messages) {
    total_chars += message.size();
  }

  if (total_chars == 0U) {
    return 0U;
  }

  return static_cast<std::uint32_t>((total_chars / 4U) + messages.size());
}

[[nodiscard]] std::string budget_tier(const StageLlmCallRequest& request,
                                      const std::uint32_t target_output_tokens) {
  if (!request.budget_context.has_value()) {
    return "unspecified";
  }

  const auto& budget = *request.budget_context;
  if (budget.near_budget_limit || budget.budget_utilization >= 0.80F ||
      (target_output_tokens > 0U && budget.remaining_tokens <= target_output_tokens)) {
    return "tight";
  }

  if (!budget.context_was_truncated && budget.budget_utilization <= 0.25F) {
    return "open";
  }

  return "balanced";
}

[[nodiscard]] std::string response_format(const StageLlmCallRequest& request) {
  if (!requires_structured_output(request)) {
    return "text";
  }

  if (request.schema_spec.schema_kind == StageSchemaKind::JsonSchema) {
    return "json_schema";
  }

  return "json_object";
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

bool replace_json_assignment(std::string& text,
                             const std::string_view& key,
                             std::vector<std::string>& warnings) {
  const std::string pattern = "\"" + std::string(key) + "\":\"";
  bool replaced = false;
  std::size_t position = 0U;
  while ((position = text.find(pattern, position)) != std::string::npos) {
    const auto value_begin = position + pattern.size();
    const auto value_end = text.find('"', value_begin);
    if (value_end == std::string::npos) {
      break;
    }
    text.replace(value_begin, value_end - value_begin, "[REDACTED]");
    append_unique(warnings, std::string("provider_private_redacted:") + std::string(key));
    position = value_begin + std::string_view{"[REDACTED]"}.size();
    replaced = true;
  }

  return replaced;
}

bool replace_plain_assignment(std::string& text,
                              const std::string_view& key,
                              std::vector<std::string>& warnings) {
  const std::string pattern = std::string(key) + "=";
  bool replaced = false;
  std::size_t position = 0U;
  while ((position = text.find(pattern, position)) != std::string::npos) {
    const auto value_begin = position + pattern.size();
    const auto value_end = text.find_first_of(",; \n\r\t}", value_begin);
    const auto effective_end = value_end == std::string::npos ? text.size() : value_end;
    text.replace(value_begin, effective_end - value_begin, "[REDACTED]");
    append_unique(warnings, std::string("provider_private_redacted:") + std::string(key));
    position = value_begin + std::string_view{"[REDACTED]"}.size();
    replaced = true;
  }

  return replaced;
}

[[nodiscard]] std::string sanitize_payload(std::string payload,
                                           std::vector<std::string>& warnings) {
  static constexpr std::string_view kSensitiveKeys[] = {
      "reasoning_content",
      "reasoning_trace",
      "raw_prompt",
      "prompt_bundle",
      "provider_payload",
      "api_token",
      "authorization",
      "secret_key",
  };

  for (const auto& key : kSensitiveKeys) {
    replace_json_assignment(payload, key, warnings);
    replace_plain_assignment(payload, key, warnings);
  }

  return payload;
}

[[nodiscard]] std::string to_lower_copy(std::string value) {
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

[[nodiscard]] bool release_scope_matches(const std::optional<std::string>& release_scope,
                                         const std::string_view expected) {
  return release_scope.has_value() && to_lower_copy(*release_scope) == expected;
}

[[nodiscard]] std::string prompt_eval_status_name(const contracts::PromptEvalStatus status) {
  switch (status) {
    case contracts::PromptEvalStatus::Draft:
      return "draft";
    case contracts::PromptEvalStatus::Experiment:
      return "experiment";
    case contracts::PromptEvalStatus::Canary:
      return "canary";
    case contracts::PromptEvalStatus::Stable:
      return "stable";
    case contracts::PromptEvalStatus::Deprecated:
      return "deprecated";
    case contracts::PromptEvalStatus::Unspecified:
      return "unspecified";
  }

  return "unspecified";
}

[[nodiscard]] std::optional<std::string> prompt_release_diagnostic(
    const contracts::LLMResponse& response) {
  if ((response.eval_status.has_value() &&
       *response.eval_status == contracts::PromptEvalStatus::Deprecated) ||
      release_scope_matches(response.release_scope, "retired") ||
      release_scope_matches(response.release_scope, "prompt_retired")) {
    return std::string{"prompt_retired"};
  }

  if (release_scope_matches(response.release_scope, "blocked") ||
      release_scope_matches(response.release_scope, "eval_blocked")) {
    return std::string{"eval_blocked"};
  }

  return std::nullopt;
}

[[nodiscard]] std::string prompt_release_error_message(
    const std::string_view diagnostic) {
  if (diagnostic == "prompt_retired") {
    return "llm response metadata indicates the selected prompt release is retired";
  }

  return "llm response metadata indicates the selected prompt evaluation is blocked";
}

[[nodiscard]] contracts::ResultCode fallback_result_code(
    const std::optional<dasall::llm::LLMFailureCategory>& failure_category) {
  if (!failure_category.has_value()) {
    return contracts::ResultCode::RuntimeRetryExhausted;
  }

  switch (*failure_category) {
    case dasall::llm::LLMFailureCategory::PromptAsset:
    case dasall::llm::LLMFailureCategory::PromptGovernance:
    case dasall::llm::LLMFailureCategory::Routing:
      return contracts::ResultCode::PolicyDenied;
    case dasall::llm::LLMFailureCategory::AdapterTransport:
    case dasall::llm::LLMFailureCategory::ProviderProtocol:
    case dasall::llm::LLMFailureCategory::FallbackExhausted:
      return contracts::ResultCode::ProviderTimeout;
  }

  return contracts::ResultCode::RuntimeRetryExhausted;
}

[[nodiscard]] std::string failure_category_name(
    const std::optional<dasall::llm::LLMFailureCategory>& failure_category) {
  if (!failure_category.has_value()) {
    return "unknown";
  }

  switch (*failure_category) {
    case dasall::llm::LLMFailureCategory::PromptAsset:
      return "prompt_asset";
    case dasall::llm::LLMFailureCategory::PromptGovernance:
      return "prompt_governance";
    case dasall::llm::LLMFailureCategory::Routing:
      return "routing";
    case dasall::llm::LLMFailureCategory::AdapterTransport:
      return "adapter_transport";
    case dasall::llm::LLMFailureCategory::ProviderProtocol:
      return "provider_protocol";
    case dasall::llm::LLMFailureCategory::FallbackExhausted:
      return "fallback_exhausted";
  }

  return "unknown";
}

void append_route_diagnostic(StageLlmCallResult& result) {
  if (!result.resolved_route.empty()) {
    result.diagnostics.push_back(std::string("route:") + result.resolved_route);
  }
}

void append_error_type_diagnostic(StageLlmCallResult& result,
                                  const std::optional<contracts::ErrorInfo>& error_info) {
  if (!error_info.has_value() || !error_info->failure_type.has_value()) {
    return;
  }

  result.diagnostics.push_back(
      std::string("error_type:") +
      std::string(contracts::result_code_category_name(*error_info->failure_type)));
}

[[nodiscard]] contracts::ErrorInfo make_error_info(const contracts::ResultCode result_code,
                                                   const std::string& stage_name,
                                                   std::string message) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(result_code),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(result_code),
          .message = std::move(message),
          .stage = stage_name,
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition.llm_bridge",
          .ref_id = stage_name,
      },
  };
}

}  // namespace

CognitionLlmBridge::CognitionLlmBridge(std::shared_ptr<dasall::llm::ILLMManager> llm_manager)
    : llm_manager_(std::move(llm_manager)) {}

StageBudgetHint CognitionLlmBridge::derive_budget_hint(const StageLlmCallRequest& request) const {
  StageBudgetHint hint;
  hint.estimated_input_tokens = estimate_input_tokens(request.messages);
  hint.target_output_tokens = request.model_hint.max_output_tokens;
  if (request.budget_context.has_value()) {
    hint.remaining_tokens = request.budget_context->remaining_tokens;
    hint.near_budget_limit = request.budget_context->near_budget_limit;
    if (hint.target_output_tokens == 0U ||
        (hint.remaining_tokens > 0U && hint.remaining_tokens < hint.target_output_tokens)) {
      hint.target_output_tokens = hint.remaining_tokens;
    }
  }
  hint.budget_tier = budget_tier(request, hint.target_output_tokens);
  return hint;
}

dasall::llm::LLMGenerateRequest CognitionLlmBridge::build_llm_request(
    const StageLlmCallRequest& request) const {
  const auto stage_name = resolve_stage_name(request);
  const auto task_type = resolve_task_type(request);
  const auto budget_hint = derive_budget_hint(request);

  dasall::llm::LLMGenerateRequest llm_request;
  llm_request.stage = stage_name;
  llm_request.task_type = task_type;
  llm_request.request.request_id = request.request_id;
  llm_request.request.llm_call_id =
      request.llm_call_id.empty() ? request.request_id + ":" + stage_name : request.llm_call_id;
  llm_request.request.request_mode = request.prefer_streaming
                                         ? contracts::LLMRequestMode::Streaming
                                         : contracts::LLMRequestMode::Unary;
  llm_request.request.messages = request.messages;
  llm_request.request.created_at = current_time_ms();
  llm_request.request.max_output_tokens = budget_hint.target_output_tokens;
  if (request.model_hint.deadline_ms > 0U) {
    llm_request.request.timeout_ms = request.model_hint.deadline_ms;
  }
  if (!request.model_hint.preferred_provider.empty()) {
    llm_request.request.model_route = request.model_hint.preferred_provider;
  }
  llm_request.request.response_format = response_format(request);
  if (requires_structured_output(request) && !request.schema_spec.output_schema_ref.empty()) {
    llm_request.request.output_schema_ref = request.schema_spec.output_schema_ref;
  }

  if (request.budget_context.has_value()) {
    llm_request.request.runtime_budget = contracts::RuntimeBudget{
        .max_tokens = request.budget_context->total_budget_tokens,
        .max_turns = std::nullopt,
        .max_tool_calls = std::nullopt,
        .max_latency_ms = request.model_hint.deadline_ms > 0U
                              ? std::optional<std::uint32_t>{request.model_hint.deadline_ms}
                              : std::nullopt,
        .max_replan_count = std::nullopt,
    };
  }

  llm_request.request.tags = std::vector<std::string>{
      "cognition",
      std::string("stage:") + stage_name,
      std::string("task:") + task_type,
  };

  llm_request.selection_hint = std::make_shared<const dasall::llm::ModelSelectionHint>(
      dasall::llm::ModelSelectionHint{
          .stage = stage_name,
          .task_type = task_type,
          .complexity_tier = complexity_tier_name(request.model_hint.capability_tier),
          .latency_sla_tier = latency_sla_tier(request.model_hint.deadline_ms),
          .budget_tier = budget_hint.budget_tier,
          .requires_tools = (stage_name == "execution" && task_type == "action_decision"),
          .requires_reasoning = request.model_hint.requires_reasoning_trace,
          .prefers_visible_reasoning = request.model_hint.requires_reasoning_trace,
          .estimated_input_tokens = budget_hint.estimated_input_tokens,
          .target_output_tokens = budget_hint.target_output_tokens,
          .previous_route_failures = 0U,
      });

  return llm_request;
}

LlmFailureProjection CognitionLlmBridge::project_llm_failure(
    const StageLlmCallRequest& request,
    const dasall::llm::LLMManagerResult& manager_result) const {
  const auto stage_name = resolve_stage_name(request);
  const auto result_code = manager_result.code.value_or(
      fallback_result_code(manager_result.failure_category));

  contracts::ErrorInfo error_info = manager_result.error.value_or(
      make_error_info(result_code, stage_name, "llm manager returned a failure result"));
  error_info.failure_type = contracts::classify_result_code(result_code);
  error_info.retryable = false;
  error_info.safe_to_replan = false;
  error_info.details.code = static_cast<int>(result_code);
  error_info.details.stage = stage_name;
  if (error_info.details.message.empty()) {
    error_info.details.message = "llm manager returned a failure result";
  }
  error_info.source_ref = contracts::ErrorSourceRefMinimal{
      .ref_type = "cognition.llm_bridge",
      .ref_id = stage_name,
  };

  return LlmFailureProjection{
      .result_code = result_code,
      .error_info = std::move(error_info),
      .diagnostic = std::string("llm_failure:") +
                    failure_category_name(manager_result.failure_category),
  };
}

StageLlmCallResult CognitionLlmBridge::normalize_llm_response(
    const StageLlmCallRequest& request,
    const dasall::llm::LLMManagerResult& manager_result) const {
  StageLlmCallResult result;
  result.budget_hint = derive_budget_hint(request);
  result.resolved_route = manager_result.resolved_route;
  result.fallback_used = manager_result.fallback_used;
  result.diagnostics.push_back(std::string("stage:") + resolve_stage_name(request));
  result.diagnostics.push_back(std::string("task:") + resolve_task_type(request));
  append_route_diagnostic(result);

  if (result.budget_hint.near_budget_limit) {
    result.warnings.push_back("budget:near_limit");
  }

  if (!manager_result.has_consistent_values()) {
    const auto failure = LlmFailureProjection{
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = make_error_info(contracts::ResultCode::RuntimeRetryExhausted,
                                      resolve_stage_name(request),
                                      "llm manager returned an inconsistent result"),
        .diagnostic = "llm_failure:inconsistent_result",
    };
    result.result_code = failure.result_code;
    result.error_info = failure.error_info;
    result.diagnostics.push_back(failure.diagnostic);
    append_error_type_diagnostic(result, result.error_info);
    return result;
  }

  if (manager_result.error.has_value() || manager_result.code.has_value()) {
    const auto failure = project_llm_failure(request, manager_result);
    result.result_code = failure.result_code;
    result.error_info = failure.error_info;
    result.diagnostics.push_back(failure.diagnostic);
    append_error_type_diagnostic(result, result.error_info);
    return result;
  }

  if (!manager_result.response.has_value()) {
    const auto failure = LlmFailureProjection{
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = make_error_info(contracts::ResultCode::RuntimeRetryExhausted,
                                      resolve_stage_name(request),
                                      "llm manager returned no response payload"),
        .diagnostic = "llm_failure:empty_response",
    };
    result.result_code = failure.result_code;
    result.error_info = failure.error_info;
    result.diagnostics.push_back(failure.diagnostic);
    append_error_type_diagnostic(result, result.error_info);
    return result;
  }

  auto response = *manager_result.response;
  if (response.content_payload.has_value()) {
    response.content_payload = sanitize_payload(*response.content_payload, result.warnings);
  }

  if (const auto release_diagnostic = prompt_release_diagnostic(response);
      release_diagnostic.has_value()) {
    result.result_code = contracts::ResultCode::PolicyDenied;
    result.error_info = make_error_info(contracts::ResultCode::PolicyDenied,
                                        resolve_stage_name(request),
                                        prompt_release_error_message(*release_diagnostic));
    result.diagnostics.push_back("llm_failure:prompt_governance");
    result.diagnostics.push_back(*release_diagnostic);
    if (response.eval_status.has_value()) {
      result.diagnostics.push_back("prompt_eval_status:" +
                                   prompt_eval_status_name(*response.eval_status));
    }
    if (response.release_scope.has_value() && !response.release_scope->empty()) {
      result.diagnostics.push_back("prompt_release_scope:" + *response.release_scope);
    }
    append_error_type_diagnostic(result, result.error_info);
    return result;
  }

  result.response = std::move(response);
  return result;
}

StageLlmCallResult CognitionLlmBridge::invoke_stage(const StageLlmCallRequest& request) const {
  StageLlmCallResult result;
  result.budget_hint = derive_budget_hint(request);

  if (!llm_manager_) {
    result.result_code = contracts::ResultCode::RuntimeRetryExhausted;
    result.error_info = make_error_info(contracts::ResultCode::RuntimeRetryExhausted,
                                        resolve_stage_name(request),
                                        "llm manager dependency is not available");
    result.diagnostics.push_back("llm_failure:manager_missing");
    append_error_type_diagnostic(result, result.error_info);
    return result;
  }

  const auto llm_request = build_llm_request(request);
  const auto manager_result = request.prefer_streaming
                                  ? llm_manager_->stream_generate(llm_request, nullptr)
                                  : llm_manager_->generate(llm_request);
  return normalize_llm_response(request, manager_result);
}

}  // namespace dasall::cognition::llm_bridge
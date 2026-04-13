#include "ResponseNormalizer.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "llm/LLMBoundaryGuards.h"

namespace {

using AdapterCallResult = dasall::llm::AdapterCallResult;
using AdapterUsageFragment = dasall::llm::AdapterUsageFragment;
using ResponseNormalizationResult = dasall::llm::execution::ResponseNormalizationResult;
using ResponseNormalizerContext = dasall::llm::execution::ResponseNormalizerContext;
using ResultCode = dasall::contracts::ResultCode;

constexpr std::string_view kNormalizerStage = "llm.response_normalizer";

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

void append_unique_audit(std::vector<std::string>& audit_events, std::string event) {
  if (event.empty()) {
    return;
  }

  const auto it = std::find(audit_events.begin(), audit_events.end(), event);
  if (it == audit_events.end()) {
    audit_events.push_back(std::move(event));
  }
}

dasall::contracts::ErrorInfo make_protocol_error(std::string route_key,
                                                 std::string message) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::classify_result_code(ResultCode::ValidationFieldMissing);
  error.retryable = false;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(ResultCode::ValidationFieldMissing);
  error.details.message = std::move(message);
  error.details.stage = std::string(kNormalizerStage);
  error.source_ref.ref_type = "route";
  error.source_ref.ref_id = std::move(route_key);
  return error;
}

std::string canonical_finish_reason(std::string_view finish_reason) {
  const std::string normalized = to_lower_copy(std::string(finish_reason));
  if (normalized == "tool_calls" || normalized == "tool_call") {
    return "tool_call";
  }

  if (normalized == "max_tokens" || normalized == "length") {
    return "length";
  }

  if (normalized == "content_filter" || normalized == "refusal") {
    return "refusal";
  }

  if (normalized == "stop" || normalized == "clarification" ||
      normalized == "replan") {
    return normalized;
  }

  return "unknown";
}

std::optional<AdapterUsageFragment> usage_fragment_from_response(
    const dasall::contracts::LLMResponse& response) {
  if (!response.input_tokens.has_value() || !response.output_tokens.has_value() ||
      !response.total_tokens.has_value()) {
    return std::nullopt;
  }

  return AdapterUsageFragment{
      .prompt_tokens = response.input_tokens,
      .completion_tokens = response.output_tokens,
      .total_tokens = response.total_tokens,
      .prompt_cache_hit_tokens = std::nullopt,
      .prompt_cache_miss_tokens = std::nullopt,
  };
}

ResponseNormalizationResult make_protocol_failure(std::string route_key,
                                                  std::vector<std::string> audit_events,
                                                  std::string reason) {
  append_unique_audit(audit_events, "malformed_payload:" + reason);
  return ResponseNormalizationResult{
      .response = std::nullopt,
      .error = make_protocol_error(std::move(route_key),
                                   "response normalizer rejected adapter payload: " + reason),
      .result_code = ResultCode::ValidationFieldMissing,
      .usage_fragment = std::nullopt,
      .audit_events = std::move(audit_events),
      .provider_trace_id = {},
      .reasoning_content_stripped = false,
  };
}

}  // namespace

namespace dasall::llm::execution {

bool ResponseNormalizerContext::has_consistent_values() const {
  return !route_key.empty() && !provider_id.empty() && !model_id.empty() &&
         completed_at_ms > 0;
}

bool ResponseNormalizationResult::succeeded() const {
  return response.has_value();
}

bool ResponseNormalizationResult::has_consistent_values() const {
  if (response.has_value() == error.has_value()) {
    return false;
  }

  if (error.has_value() != result_code.has_value()) {
    return false;
  }

  if (usage_fragment.has_value()) {
    if (!response.has_value() || !usage_fragment->has_consistent_values()) {
      return false;
    }
  }

  return true;
}

ResponseNormalizationResult ResponseNormalizer::normalize(
    const AdapterCallResult& adapter_result,
    const ResponseNormalizerContext& context) const {
  if (!context.has_consistent_values()) {
    return make_protocol_failure(context.route_key, {}, "normalizer_context_inconsistent");
  }

  if (!adapter_result.has_consistent_values()) {
    return make_protocol_failure(context.route_key, {}, "adapter_result_inconsistent");
  }

  if (adapter_result.error.has_value()) {
    std::vector<std::string> audit_events;
    append_unique_audit(audit_events, "adapter_failure_passthrough");
    return ResponseNormalizationResult{
        .response = std::nullopt,
        .error = adapter_result.error,
        .result_code = adapter_result.result_code,
        .usage_fragment = std::nullopt,
        .audit_events = std::move(audit_events),
        .provider_trace_id = adapter_result.provider_diagnostics.provider_trace_id,
        .reasoning_content_stripped = !adapter_result.provider_diagnostics.reasoning_content.empty(),
    };
  }

  if (!adapter_result.response.has_value()) {
    return make_protocol_failure(context.route_key, {}, "response_missing");
  }

  auto response = *adapter_result.response;
  std::vector<std::string> audit_events;

  if (!response.request_id.has_value() && context.request_id.has_value()) {
    response.request_id = context.request_id;
  }

  if (!response.llm_call_id.has_value() && context.llm_call_id.has_value()) {
    response.llm_call_id = context.llm_call_id;
  }

  if (!response.model_name.has_value()) {
    response.model_name = context.model_name.empty() ? context.model_id : context.model_name;
  }

  if (!response.completed_at.has_value()) {
    response.completed_at = context.completed_at_ms;
  }

  if ((!response.prompt_id.has_value() || response.prompt_id->empty()) &&
      !context.prompt_id.empty()) {
    response.prompt_id = context.prompt_id;
  }

  if ((!response.prompt_version.has_value() || response.prompt_version->empty()) &&
      !context.prompt_version.empty()) {
    response.prompt_version = context.prompt_version;
  }

  auto usage_fragment = adapter_result.usage;
  if (!usage_fragment.has_value()) {
    usage_fragment = usage_fragment_from_response(response);
  }

  if (usage_fragment.has_value()) {
    if (!usage_fragment->has_consistent_values()) {
      return make_protocol_failure(context.route_key, std::move(audit_events),
                                   "usage_fragment_inconsistent");
    }

    if (response.input_tokens.has_value() && response.output_tokens.has_value() &&
        response.total_tokens.has_value()) {
      if (*response.input_tokens != *usage_fragment->prompt_tokens ||
          *response.output_tokens != *usage_fragment->completion_tokens ||
          *response.total_tokens != *usage_fragment->total_tokens) {
        return make_protocol_failure(context.route_key, std::move(audit_events),
                                     "usage_fragment_mismatch");
      }
    }

    response.input_tokens = usage_fragment->prompt_tokens;
    response.output_tokens = usage_fragment->completion_tokens;
    response.total_tokens = usage_fragment->total_tokens;
  }

  if (!adapter_result.provider_diagnostics.reasoning_content.empty()) {
    append_unique_audit(audit_events, "reasoning_content_stripped");
  }

  for (const auto& audit_tag : adapter_result.provider_diagnostics.audit_tags) {
    append_unique_audit(audit_events, "provider_audit:" + audit_tag);
  }

  if (response.finish_reason.has_value()) {
    const std::string original_finish_reason = *response.finish_reason;
    const std::string normalized_finish_reason = canonical_finish_reason(original_finish_reason);
    if (normalized_finish_reason == "unknown") {
      append_unique_audit(audit_events, "unknown_finish_reason:" + original_finish_reason);
    }

    response.finish_reason = normalized_finish_reason;
  }

  const auto validation = contracts::validate_llm_response_field_rules(response);
  if (!validation.ok) {
    return make_protocol_failure(context.route_key, std::move(audit_events),
                                 std::string(validation.reason));
  }

  return ResponseNormalizationResult{
      .response = std::move(response),
      .error = std::nullopt,
      .result_code = std::nullopt,
      .usage_fragment = std::move(usage_fragment),
      .audit_events = std::move(audit_events),
      .provider_trace_id = adapter_result.provider_diagnostics.provider_trace_id,
      .reasoning_content_stripped = !adapter_result.provider_diagnostics.reasoning_content.empty(),
  };
}

}  // namespace dasall::llm::execution
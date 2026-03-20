#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "llm/LLMRequest.h"
#include "llm/LLMResponse.h"

namespace dasall::contracts {

// LLMRequestBoundaryDecision classifies the forbidden field families rejected
// by the llm request boundary.
enum class LLMRequestBoundaryDecision {
  AllowField,
  RejectContextOwnershipField,
  RejectPromptAssetField,
  RejectProviderPrivateField,
  RejectRuntimeControlField,
};

// LLMResponseBoundaryDecision classifies the forbidden field families rejected
// by the llm response boundary.
enum class LLMResponseBoundaryDecision {
  AllowField,
  RejectProviderPrivateField,
  RejectPromptContextField,
  RejectExecutionControlField,
  RejectErrorOwnershipField,
};

// LLMRequestFieldBoundaryResult reports whether a field name is allowed for the
// request object and, if not, which forbidden family it belongs to.
struct LLMRequestFieldBoundaryResult {
  bool allowed = true;
  LLMRequestBoundaryDecision decision = LLMRequestBoundaryDecision::AllowField;
  std::string_view reason = "llm request field is allowed by WP05-T010";
};

// LLMResponseFieldBoundaryResult reports whether a field name is allowed for
// the response object and, if not, which forbidden family it belongs to.
struct LLMResponseFieldBoundaryResult {
  bool allowed = true;
  LLMResponseBoundaryDecision decision = LLMResponseBoundaryDecision::AllowField;
  std::string_view reason = "llm response field is allowed by WP05-T010";
};

// LLMGuardResult is the common result shape for request/response validation.
struct LLMGuardResult {
  bool ok = false;
  std::string_view reason = "llm contract validation failed";
};

// Context/memory ownership fields must remain upstream of LLMRequest.
inline constexpr std::array<std::string_view, 3>
    kLLMRequestContextOwnershipForbiddenFields = {
        "context_packet",
        "summary_memory",
        "retrieval_candidates",
};

// Prompt asset source fields belong to PromptSpec / PromptRelease rather than
// the normalized llm request handoff object.
inline constexpr std::array<std::string_view, 4>
    kLLMRequestPromptAssetForbiddenFields = {
        "system_instructions",
        "task_template",
        "few_shot_refs",
        "policy_notes",
};

// Provider-private transport or vendor parameter fields must not leak into the
// shared request contract.
inline constexpr std::array<std::string_view, 3>
    kLLMRequestProviderPrivateForbiddenFields = {
        "provider_payload",
        "model_provider_args",
        "vendor_request",
};

// Retry/checkpoint/FSM state remains a runtime concern, not a shared llm
// contract concern.
inline constexpr std::array<std::string_view, 3>
    kLLMRequestRuntimeControlForbiddenFields = {
        "retry_count",
        "checkpoint_ref",
        "fsm_state",
};

// Provider raw payload and vendor diagnostics must not leak into the shared
// response contract.
inline constexpr std::array<std::string_view, 4>
    kLLMResponseProviderPrivateForbiddenFields = {
        "raw_provider_response",
        "logprobs",
        "reasoning_trace",
        "vendor_response",
};

// Prompt/context ownership remains upstream of LLMResponse.
inline constexpr std::array<std::string_view, 3>
    kLLMResponsePromptContextForbiddenFields = {
        "messages",
        "system_instructions",
        "context_packet",
};

// LLMResponse carries intent only and must not embed executable runtime
// control commands.
inline constexpr std::array<std::string_view, 4>
    kLLMResponseExecutionControlForbiddenFields = {
        "shell_command",
        "tool_request",
        "worker_dispatch",
        "retry_plan",
};

// Error objects stay in the cross-cutting error subdomain.
inline constexpr std::array<std::string_view, 3>
    kLLMResponseErrorOwnershipForbiddenFields = {
        "error_info",
        "result_code",
        "failure_type",
};

// Validates the required request fields frozen by WP05-T010.
inline LLMGuardResult validate_llm_request_required_fields(
    const LLMRequest& request) {
  if (!has_non_empty_value(request.request_id)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.llm_call_id)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "llm_call_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(request.model_route)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "model_route is required and must be non-empty",
    };
  }

  if (!request.request_mode.has_value() ||
      *request.request_mode == LLMRequestMode::Unspecified) {
    return LLMGuardResult{
        .ok = false,
        .reason = "request_mode is required and must not be Unspecified",
    };
  }

  if (!request.messages.has_value() || request.messages->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "messages are required and must contain at least one item",
    };
  }

  for (const auto& message : *request.messages) {
    if (message.empty()) {
      return LLMGuardResult{
          .ok = false,
          .reason = "messages must not contain empty-string elements",
      };
    }
  }

  if (!request.created_at.has_value() || *request.created_at <= 0) {
    return LLMGuardResult{
        .ok = false,
        .reason = "created_at is required and must be a positive timestamp",
    };
  }

  return LLMGuardResult{
      .ok = true,
      .reason = "all required llm request fields present",
  };
}

// Validates enum range and layered identity constraints for LLMRequest.
inline LLMGuardResult validate_llm_request_boundary(const LLMRequest& request) {
  const auto required_result = validate_llm_request_required_fields(request);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_mode = static_cast<int>(*request.request_mode);
  if (raw_mode < static_cast<int>(LLMRequestMode::Unary) ||
      raw_mode > static_cast<int>(LLMRequestMode::Streaming)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "request_mode value is outside the known enum range",
    };
  }

  if (*request.llm_call_id == *request.request_id) {
    return LLMGuardResult{
        .ok = false,
        .reason = "llm_call_id must not equal request_id because request identity and llm call identity are layered separately",
    };
  }

  return LLMGuardResult{
      .ok = true,
      .reason = "llm request boundary validation passed",
  };
}

// Validates optional request fields and shared budget/tag rules.
inline LLMGuardResult validate_llm_request_field_rules(
    const LLMRequest& request) {
  const auto boundary_result = validate_llm_request_boundary(request);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  const bool prompt_id_present = request.prompt_id.has_value();
  const bool prompt_version_present = request.prompt_version.has_value();
  if (prompt_id_present != prompt_version_present) {
    return LLMGuardResult{
        .ok = false,
        .reason = "prompt_id and prompt_version must either both be present or both be absent",
    };
  }

  if (prompt_id_present && request.prompt_id->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "prompt_id must be non-empty when present",
    };
  }

  if (prompt_version_present && request.prompt_version->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "prompt_version must be non-empty when present",
    };
  }

  if (request.output_schema_ref.has_value() && request.output_schema_ref->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "output_schema_ref must be non-empty when present",
    };
  }

  if (request.response_format.has_value() && request.response_format->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "response_format must be non-empty when present",
    };
  }

  if (request.max_output_tokens.has_value() && *request.max_output_tokens == 0U) {
    return LLMGuardResult{
        .ok = false,
        .reason = "max_output_tokens must be positive when present",
    };
  }

  if (request.timeout_ms.has_value() && *request.timeout_ms == 0U) {
    return LLMGuardResult{
        .ok = false,
        .reason = "timeout_ms must be positive when present",
    };
  }

  if (request.runtime_budget.has_value()) {
    const auto& budget = *request.runtime_budget;
    if (budget.max_tokens.has_value() && *budget.max_tokens == 0U) {
      return LLMGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_tokens must be positive when present",
      };
    }
    if (budget.max_turns.has_value() && *budget.max_turns == 0U) {
      return LLMGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_turns must be positive when present",
      };
    }
    if (budget.max_tool_calls.has_value() && *budget.max_tool_calls == 0U) {
      return LLMGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_tool_calls must be positive when present",
      };
    }
    if (budget.max_latency_ms.has_value() && *budget.max_latency_ms == 0U) {
      return LLMGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_latency_ms must be positive when present",
      };
    }
    if (budget.max_replan_count.has_value() &&
        *budget.max_replan_count == 0U) {
      return LLMGuardResult{
          .ok = false,
          .reason = "runtime_budget.max_replan_count must be positive when present",
      };
    }
  }

  if (request.tags.has_value()) {
    if (request.tags->empty()) {
      return LLMGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < request.tags->size(); ++index) {
      if ((*request.tags)[index].empty()) {
        return LLMGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < request.tags->size(); ++probe) {
        if ((*request.tags)[index] == (*request.tags)[probe]) {
          return LLMGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return LLMGuardResult{
      .ok = true,
      .reason = "llm request field rules validation passed",
  };
}

// Maps forbidden request field names to stable rejection families.
constexpr LLMRequestFieldBoundaryResult evaluate_llm_request_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kLLMRequestContextOwnershipForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMRequestFieldBoundaryResult{
          .allowed = false,
          .decision = LLMRequestBoundaryDecision::RejectContextOwnershipField,
          .reason = "llm request must not own context or memory fields",
      };
    }
  }

  for (const auto forbidden_field : kLLMRequestPromptAssetForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMRequestFieldBoundaryResult{
          .allowed = false,
          .decision = LLMRequestBoundaryDecision::RejectPromptAssetField,
          .reason = "llm request must not contain prompt asset source fields",
      };
    }
  }

  for (const auto forbidden_field : kLLMRequestProviderPrivateForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMRequestFieldBoundaryResult{
          .allowed = false,
          .decision = LLMRequestBoundaryDecision::RejectProviderPrivateField,
          .reason = "llm request must not contain provider-private transport or vendor parameter fields",
      };
    }
  }

  for (const auto forbidden_field : kLLMRequestRuntimeControlForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMRequestFieldBoundaryResult{
          .allowed = false,
          .decision = LLMRequestBoundaryDecision::RejectRuntimeControlField,
          .reason = "llm request must not contain runtime retry or checkpoint control fields",
      };
    }
  }

  return LLMRequestFieldBoundaryResult{};
}

// Validates the required response fields frozen by WP05-T010.
inline LLMGuardResult validate_llm_response_required_fields(
    const LLMResponse& response) {
  if (!has_non_empty_value(response.request_id)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "request_id is required and must be non-empty",
    };
  }

  if (!has_non_empty_value(response.llm_call_id)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "llm_call_id is required and must be non-empty",
    };
  }

  if (!response.response_kind.has_value() ||
      *response.response_kind == LLMResponseKind::Unspecified) {
    return LLMGuardResult{
        .ok = false,
        .reason = "response_kind is required and must not be Unspecified",
    };
  }

  if (!has_non_empty_value(response.content_payload)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "content_payload is required and must be non-empty",
    };
  }

  if (!response.completed_at.has_value() || *response.completed_at <= 0) {
    return LLMGuardResult{
        .ok = false,
        .reason = "completed_at is required and must be a positive timestamp",
    };
  }

  return LLMGuardResult{
      .ok = true,
      .reason = "all required llm response fields present",
  };
}

// Validates enum range and layered identity constraints for LLMResponse.
inline LLMGuardResult validate_llm_response_boundary(
    const LLMResponse& response) {
  const auto required_result = validate_llm_response_required_fields(response);
  if (!required_result.ok) {
    return required_result;
  }

  const int raw_kind = static_cast<int>(*response.response_kind);
  if (raw_kind < static_cast<int>(LLMResponseKind::DirectResponse) ||
      raw_kind > static_cast<int>(LLMResponseKind::Refusal)) {
    return LLMGuardResult{
        .ok = false,
        .reason = "response_kind value is outside the known enum range",
    };
  }

  if (*response.llm_call_id == *response.request_id) {
    return LLMGuardResult{
        .ok = false,
        .reason = "llm_call_id must not equal request_id because request identity and llm call identity are layered separately",
    };
  }

  return LLMGuardResult{
      .ok = true,
      .reason = "llm response boundary validation passed",
  };
}

// Validates optional response fields, refusal consistency, and usage-accounting
// consistency.
inline LLMGuardResult validate_llm_response_field_rules(
    const LLMResponse& response) {
  const auto boundary_result = validate_llm_response_boundary(response);
  if (!boundary_result.ok) {
    return boundary_result;
  }

  if (response.model_name.has_value() && response.model_name->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "model_name must be non-empty when present",
    };
  }

  const bool prompt_id_present = response.prompt_id.has_value();
  const bool prompt_version_present = response.prompt_version.has_value();
  if (prompt_id_present != prompt_version_present) {
    return LLMGuardResult{
        .ok = false,
        .reason = "prompt_id and prompt_version must either both be present or both be absent",
    };
  }

  if (prompt_id_present && response.prompt_id->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "prompt_id must be non-empty when present",
    };
  }

  if (prompt_version_present && response.prompt_version->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "prompt_version must be non-empty when present",
    };
  }

  if (response.finish_reason.has_value() && response.finish_reason->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "finish_reason must be non-empty when present",
    };
  }

  const bool refusal_present = response.refusal_reason.has_value();
  if (refusal_present && response.refusal_reason->empty()) {
    return LLMGuardResult{
        .ok = false,
        .reason = "refusal_reason must be non-empty when present",
    };
  }

  if (*response.response_kind == LLMResponseKind::Refusal && !refusal_present) {
    return LLMGuardResult{
        .ok = false,
        .reason = "refusal_reason is required when response_kind is Refusal",
    };
  }

  if (*response.response_kind != LLMResponseKind::Refusal && refusal_present) {
    return LLMGuardResult{
        .ok = false,
        .reason = "refusal_reason must be absent unless response_kind is Refusal",
    };
  }

  const bool input_present = response.input_tokens.has_value();
  const bool output_present = response.output_tokens.has_value();
  const bool total_present = response.total_tokens.has_value();
  if (input_present || output_present || total_present) {
    if (!(input_present && output_present && total_present)) {
      return LLMGuardResult{
          .ok = false,
          .reason = "input_tokens, output_tokens, and total_tokens must either all be present or all be absent",
      };
    }

    if (*response.total_tokens !=
        static_cast<std::uint32_t>(*response.input_tokens + *response.output_tokens)) {
      return LLMGuardResult{
          .ok = false,
          .reason = "total_tokens must equal input_tokens + output_tokens",
      };
    }
  }

  if (response.tags.has_value()) {
    if (response.tags->empty()) {
      return LLMGuardResult{
          .ok = false,
          .reason = "tags must contain at least one item when present",
      };
    }

    for (std::size_t index = 0; index < response.tags->size(); ++index) {
      if ((*response.tags)[index].empty()) {
        return LLMGuardResult{
            .ok = false,
            .reason = "tags must not contain empty strings",
        };
      }

      for (std::size_t probe = index + 1; probe < response.tags->size(); ++probe) {
        if ((*response.tags)[index] == (*response.tags)[probe]) {
          return LLMGuardResult{
              .ok = false,
              .reason = "tags must not contain duplicate items",
          };
        }
      }
    }
  }

  return LLMGuardResult{
      .ok = true,
      .reason = "llm response field rules validation passed",
  };
}

// Maps forbidden response field names to stable rejection families.
constexpr LLMResponseFieldBoundaryResult evaluate_llm_response_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden_field : kLLMResponseProviderPrivateForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMResponseFieldBoundaryResult{
          .allowed = false,
          .decision = LLMResponseBoundaryDecision::RejectProviderPrivateField,
          .reason = "llm response must not contain provider-private payload or diagnostics fields",
      };
    }
  }

  for (const auto forbidden_field : kLLMResponsePromptContextForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMResponseFieldBoundaryResult{
          .allowed = false,
          .decision = LLMResponseBoundaryDecision::RejectPromptContextField,
          .reason = "llm response must not own prompt or context fields",
      };
    }
  }

  for (const auto forbidden_field : kLLMResponseExecutionControlForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMResponseFieldBoundaryResult{
          .allowed = false,
          .decision = LLMResponseBoundaryDecision::RejectExecutionControlField,
          .reason = "llm response must not contain executable runtime control fields",
      };
    }
  }

  for (const auto forbidden_field : kLLMResponseErrorOwnershipForbiddenFields) {
    if (field_name == forbidden_field) {
      return LLMResponseFieldBoundaryResult{
          .allowed = false,
          .decision = LLMResponseBoundaryDecision::RejectErrorOwnershipField,
          .reason = "llm response must not own cross-cutting error fields",
      };
    }
  }

  return LLMResponseFieldBoundaryResult{};
}

}  // namespace dasall::contracts
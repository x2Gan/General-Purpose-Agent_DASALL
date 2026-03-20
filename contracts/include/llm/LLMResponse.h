#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// LLMResponseKind freezes the stable semantic result kinds emitted by the llm
// subdomain. The enum mirrors the architecture document's output-intent model
// and adds Refusal as an explicit machine-readable branch.
enum class LLMResponseKind {
  Unspecified = 0,
  DirectResponse = 1,
  ToolCallIntent = 2,
  ClarificationRequest = 3,
  ReplanSuggestion = 4,
  Refusal = 5,
};

// LLMResponse is the provider-neutral semantic response contract introduced by
// WP05-T010.
//
// Semantic boundary (WP05-T010 frozen):
//   Allowed:
//     1. Call identity: request_id, llm_call_id
//     2. Semantic outcome: response_kind, content_payload
//     3. Audit metadata: completed_at, model_name, prompt_id, prompt_version,
//        finish_reason
//     4. Usage metadata: input_tokens, output_tokens, total_tokens
//     5. Refusal metadata: refusal_reason
//     6. Tags: provider-neutral audit / routing tags
//
//   Forbidden:
//     - Provider raw payload and vendor diagnostics
//     - Prompt or context ownership fields
//     - Executable runtime control fields
//     - ErrorInfo / ResultCode ownership fields
struct LLMResponse {
  // Request-level correlation identifier propagated from AgentRequest.
  std::optional<std::string> request_id;

  // Stable identity for the llm invocation that produced this response.
  std::optional<std::string> llm_call_id;

  // Provider-neutral semantic result kind.
  std::optional<LLMResponseKind> response_kind;

  // Normalized semantic payload. For ToolCallIntent or structured branches,
  // this is a provider-neutral serialized payload rather than vendor raw data.
  std::optional<std::string> content_payload;

  // Completion timestamp in milliseconds.
  std::optional<std::int64_t> completed_at;

  // Auditable resolved model name.
  std::optional<std::string> model_name;

  // Audit identifier of the prompt asset used for the invocation.
  std::optional<std::string> prompt_id;

  // Audit version of the prompt asset used for the invocation.
  std::optional<std::string> prompt_version;

  // Provider-neutral completion summary such as stop/length/tool_call.
  std::optional<std::string> finish_reason;

  // Count of input tokens consumed by the invocation.
  std::optional<std::uint32_t> input_tokens;

  // Count of output tokens produced by the invocation.
  std::optional<std::uint32_t> output_tokens;

  // Total token count for audit and budgeting.
  std::optional<std::uint32_t> total_tokens;

  // Explicit refusal reason when response_kind == Refusal.
  std::optional<std::string> refusal_reason;

  // Audit and routing tags that remain provider-neutral.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
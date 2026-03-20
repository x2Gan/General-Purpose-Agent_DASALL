#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::contracts {

// LLMRequestMode freezes the stable request delivery modes supported by the
// llm subdomain. The Unspecified sentinel follows the repository-wide enum
// lifecycle rule and must be rejected by guards.
enum class LLMRequestMode {
  Unspecified = 0,
  Unary = 1,
  Streaming = 2,
};

// LLMRequest is the provider-neutral model invocation contract introduced by
// WP05-T010.
//
// Semantic boundary (WP05-T010 frozen):
//   Allowed:
//     1. Call identity: request_id, llm_call_id
//     2. Routing and delivery: model_route, request_mode
//     3. Prompt handoff: messages, prompt_id, prompt_version
//     4. Structured-output hints: output_schema_ref, response_format
//     5. Execution hints: runtime_budget, max_output_tokens, timeout_ms
//     6. Audit metadata: created_at, tags
//
//   Forbidden:
//     - Context / memory ownership fields
//     - Prompt asset source fields
//     - Provider private transport or vendor parameter fields
//     - Runtime retry / checkpoint / FSM control state
struct LLMRequest {
  // Request-level correlation identifier propagated from AgentRequest.
  std::optional<std::string> request_id;

  // Stable identity for one concrete llm invocation attempt.
  std::optional<std::string> llm_call_id;

  // Provider-neutral model route identifier selected by routing policy.
  std::optional<std::string> model_route;

  // Delivery mode for the invocation.
  std::optional<LLMRequestMode> request_mode;

  // Provider-neutral message payload received from PromptComposer/
  // PromptPolicy handoff.
  std::optional<std::vector<std::string>> messages;

  // Request creation timestamp in milliseconds.
  std::optional<std::int64_t> created_at;

  // Audit identifier of the prompt asset selected for this invocation.
  std::optional<std::string> prompt_id;

  // Audit version of the selected prompt release.
  std::optional<std::string> prompt_version;

  // Structured-output schema reference when the call expects constrained data.
  std::optional<std::string> output_schema_ref;

  // Provider-neutral response format hint such as json_schema/json_object/text.
  std::optional<std::string> response_format;

  // Reuses the shared runtime budget surface rather than redefining budget
  // dimensions inside the llm subdomain.
  std::optional<RuntimeBudget> runtime_budget;

  // Upper bound for generated output tokens.
  std::optional<std::uint32_t> max_output_tokens;

  // Per-call timeout hint in milliseconds.
  std::optional<std::uint32_t> timeout_ms;

  // Audit and routing tags that stay provider-neutral.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts
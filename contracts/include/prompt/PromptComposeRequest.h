#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// CompositionStage enumerates the orchestration stage for which a Prompt
// assembly is being requested, as defined by WP04-T002-D §2.1.
//
// Stage is a required field of PromptComposeRequest (ADR-006 §6.2).
// PromptRegistry uses stage to select the appropriate PromptSpec template;
// without it, PromptComposer cannot determine which Prompt to apply.
//
// WP02-T012 enum lifecycle rule: all enums must include an explicit
// Unspecified sentinel at value 0, which guards must reject.
// ---------------------------------------------------------------------------
enum class CompositionStage {
  Unspecified = 0,  // WP02-T012 sentinel — guards must reject this value.
  Planning    = 1,  // Planner stage: compose Prompt for plan generation.
  Execution   = 2,  // Execution stage: compose Prompt after tool call / action.
  Reflection  = 3,  // Reflection stage: compose Prompt for failure attribution.
  Response    = 4,  // Response stage: compose Prompt for final reply generation.
};

// ---------------------------------------------------------------------------
// PromptComposeRequest is the assembly request object for PromptComposer,
// as defined by WP04-T002-D and ADR-006 §6.2.
//
// Main-flow chain position (WP04 Prompt chain):
//   ContextPacket → [PromptComposeRequest] → PromptComposeResult
//
// Core responsibility (ADR-006 §6.2):
//   PromptComposeRequest bundles all inputs that PromptComposer needs
//   for a single Prompt assembly operation:
//     - Which semantic context to use (via context_packet_id reference)
//     - Which stage determines the PromptSpec selection strategy
//     - Which model route and output schema to target
//     - Which tools are visible in this round
//
//   It is an assembly *request*, not a semantic context object.
//   PromptComposer resolves the ContextPacket from context_packet_id;
//   this object does not embed or replicate ContextPacket fields.
//
// Required fields (WP04-T002-D §2.2, 4 items):
//   1. request_id          — traceability to the originating AgentRequest.
//   2. stage               — CompositionStage; controls PromptSpec selection.
//   3. context_packet_id   — references the current ContextPacket by ID.
//   4. created_at          — assembly request timestamp in milliseconds.
//
// Optional fields (WP04-T002-D §2.3, 7 items):
//   5.  task_type          — type hint for fine-grained PromptSpec selection.
//   6.  prompt_release_id  — pre-selected PromptRelease; auto-selected if absent.
//   7.  visible_tools      — visible tool IDs for this round.
//   8.  model_route        — target model route; default applied if absent.
//   9.  output_schema_ref  — output structure constraint reference.
//   10. response_format    — provider response format hint.
//   11. tags               — audit / retrieval tags.
//
// Forbidden fields (WP04-T001-D §3 / ADR-006 §6.2 / §7 option-B rejected):
//   - Context-ownership fields: memory_snapshot, retrieval_candidates,
//     context_packet_internal, knowledge_fragments.
//   - These are blocked by PromptBoundaryContracts.h (T001-B).
//
// Consumers (ADR-006 §4):
//   - PromptComposer: sole consumer; maps this request to PromptComposeResult.
//
// Producer (ADR-006 §4):
//   - Runtime: assembles PromptComposeRequest from the outputs of
//     ContextOrchestrator, PromptRegistry, and the execution policy.
// ---------------------------------------------------------------------------
struct PromptComposeRequest {
  // -------------------------------------------------------------------------
  // Required fields (WP04-T002-D §2.2, 4 items)
  // -------------------------------------------------------------------------

  // Request-level unique identifier, propagated from the originating
  // AgentRequest.request_id through the entire chain.
  // Enables log/event/audit correlation.  References WP02-T009.
  std::optional<std::string> request_id;

  // Current orchestration stage for which this Prompt assembly is requested.
  // PromptRegistry uses stage to select the matching PromptSpec template.
  // Must be present and not Unspecified (ADR-006 §6.2, WP02-T012).
  std::optional<CompositionStage> stage;

  // Identifier that references the current ContextPacket for this request
  // round.  Equals ContextPacket.request_id (the common traceability key
  // established by WP02-T009).
  // PromptComposer resolves the ContextPacket from this ID, and does NOT
  // receive raw context data inline.  ADR-006 §6.2 / §3.2.
  std::optional<std::string> context_packet_id;

  // Assembly request creation timestamp in milliseconds (WP02-T010).
  // Used for freshness judgment, latency auditing, and temporal ordering.
  // Must be present and positive.
  std::optional<std::int64_t> created_at;

  // -------------------------------------------------------------------------
  // Optional fields (WP04-T002-D §2.3, 7 items)
  // "Carry meaningful content or omit" principle (WP03-T003 §4.3):
  //   - Present optional strings must be non-empty.
  //   - Present optional vectors must be non-empty with no empty-string elements.
  // -------------------------------------------------------------------------

  // Task type hint that allows PromptRegistry to apply finer-grained
  // PromptSpec selection beyond what stage alone determines.
  // Examples: "summarize", "codegen", "classify", "plan_step".
  // Absent when no task-type distinction is needed.
  std::optional<std::string> task_type;

  // Identifier of a pre-selected PromptRelease from PromptRegistry.
  // When absent, PromptComposer selects the default release for the stage.
  // When present, PromptComposer uses this specific release without
  // performing additional selection.
  std::optional<std::string> prompt_release_id;

  // Visible tool identifiers for this round, determined by the permissions
  // and tool-availability system.  PromptComposer injects these as tool
  // definitions into the assembled Prompt.
  // Absent when no tools are available or tool injection is not needed.
  std::optional<std::vector<std::string>> visible_tools;

  // Target model route identifier.  PromptComposer and PromptPolicy use
  // this to apply model-specific formatting and constraints.
  // Absent when the default model route applies.
  std::optional<std::string> model_route;

  // Output structure constraint reference (e.g., a JSON Schema identifier
  // or a named schema ref).  PromptComposer uses this to inject a structured
  // output instruction into the assembled Prompt.
  // Absent for free-format responses.  ADR-006 §6.2 ("output_schema_ref").
  std::optional<std::string> output_schema_ref;

  // Provider response format hint (e.g., "json_object", "text", "markdown").
  // PromptPolicy may override this based on model capabilities.
  // Absent when no explicit format hint is required.  ADR-006 §6.2
  // ("response_format").
  std::optional<std::string> response_format;

  // Audit and retrieval tags.  Do not carry execution control signals.
  // Consistent with the tags pattern established in AgentRequest,
  // GoalContract, ContextPacket, and other chain objects.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts

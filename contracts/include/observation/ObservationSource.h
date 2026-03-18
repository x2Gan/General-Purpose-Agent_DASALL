#pragma once

#include <string_view>

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// ObservationSource enumerates the four canonical production channels that
// emit Observations, as defined by the frozen observation pipeline
// (architecture 3.8.2, frozen plan §7).
//
// Classification (WP03-T007 frozen):
//   ToolExecution (1) — Local or remote tool execution result. MCP remote
//       tools are routed through Tool System (architecture 4.5) and therefore
//       map to ToolExecution, not a separate McpRemote enum value.
//   WorkerAgent (2)   — Worker Agent sub-task return (ADR-008 multi-agent).
//   Retrieval (3)     — Knowledge/memory retrieval result.
//   HumanFeedback (4) — Human-in-the-loop feedback, clarification, or
//       approval.
//
// Extension policy (WP03-T007):
//   New values may be added only when a genuinely new production channel
//   appears that cannot be mapped to the existing four. When adding a value:
//     1. Add to the enum with the next sequential integer.
//     2. Update ObservationSourceGuards range check.
//     3. Add correlation field rules to validate_observation_source_correlation.
//     4. Update the WP03-T007 classification table.
//   Existing values must not be removed or semantically reinterpreted.
//
// Sentinel (WP02-T012):
//   Unspecified (0) is the mandatory sentinel value for enum lifecycle.
//   Observations with source=Unspecified are rejected by Layer 1 guards.
// ---------------------------------------------------------------------------
enum class ObservationSource {
  Unspecified = 0,   // WP02-T012 sentinel: source not yet determined.
  ToolExecution = 1, // Local/remote tool execution result (Tool System output).
  WorkerAgent = 2,   // Worker Agent sub-task return (ADR-008 multi-agent).
  Retrieval = 3,     // Knowledge/memory retrieval result.
  HumanFeedback = 4, // Human-in-the-loop feedback, clarification, or approval.
};

// ---------------------------------------------------------------------------
// to_string_view returns the canonical string representation of an
// ObservationSource value. Used for logging, audit, and diagnostics.
// Does not allocate; returns a compile-time string_view.
// ---------------------------------------------------------------------------
inline constexpr std::string_view to_string_view(ObservationSource src) {
  switch (src) {
    case ObservationSource::Unspecified:   return "Unspecified";
    case ObservationSource::ToolExecution: return "ToolExecution";
    case ObservationSource::WorkerAgent:   return "WorkerAgent";
    case ObservationSource::Retrieval:     return "Retrieval";
    case ObservationSource::HumanFeedback: return "HumanFeedback";
  }
  return "Unknown";
}

// ---------------------------------------------------------------------------
// Observation source enum range constants. Used by guards and tests.
// ---------------------------------------------------------------------------
inline constexpr int kObservationSourceMin =
    static_cast<int>(ObservationSource::Unspecified);
inline constexpr int kObservationSourceMax =
    static_cast<int>(ObservationSource::HumanFeedback);

// ---------------------------------------------------------------------------
// is_known_observation_source returns true if the given raw integer falls
// within the defined ObservationSource enum range (including Unspecified).
// ---------------------------------------------------------------------------
inline constexpr bool is_known_observation_source(int raw_value) {
  return raw_value >= kObservationSourceMin &&
         raw_value <= kObservationSourceMax;
}

// ---------------------------------------------------------------------------
// source_to_error_ref_type maps an ObservationSource enum value to the
// corresponding ErrorSourceRefMinimal.ref_type string, as defined by the
// WP03-T007 alignment rules (§6 of the T007 classification table):
//
//   ToolExecution  → "tool_call"
//   WorkerAgent    → "worker_task"
//   Retrieval      → "observation"   (no dedicated ref_type)
//   HumanFeedback  → "observation"   (no dedicated ref_type)
//   Unspecified    → ""              (invalid; should not reach error path)
//
// This mapping is consumed by error-path builders that need to populate
// ErrorInfo.source_ref.ref_type consistently with the Observation source.
// ---------------------------------------------------------------------------
inline constexpr std::string_view source_to_error_ref_type(
    ObservationSource src) {
  switch (src) {
    case ObservationSource::ToolExecution: return "tool_call";
    case ObservationSource::WorkerAgent:   return "worker_task";
    case ObservationSource::Retrieval:     return "observation";
    case ObservationSource::HumanFeedback: return "observation";
    case ObservationSource::Unspecified:   return "";
  }
  return "";
}

}  // namespace dasall::contracts

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// MultiAgentResult is the collaboration-result object introduced by WP04-T016.
// It is produced by MultiAgentCoordinator / ResultMerger after worker tasks have
// run, and consumed by AgentOrchestrator to decide whether to fold the
// collaboration output into the final AgentResult, replan, degrade, or escalate.
//
// Semantic boundary (WP04-T016 frozen):
//   Allowed:
//     1. Collaboration outputs: subtask_results, merged_result
//     2. Collaboration diagnostics: conflicts, worker_trace_refs,
//        failure_summary
//     3. Orchestrator-facing advice: recommended_next_action
//
//   Forbidden:
//     - Top-level AgentResult terminal semantics (result_code, response_text,
//       task_completed, error_info)
//     - Final user-facing result wrappers (agent_result, final_agent_response)
//     - Global runtime/session/finalization state (session_id, checkpoint_ref,
//       runtime_state)
//     - Worker execution-unit fields (lease_id, worker_type, allowed_tools)
//
// Required fields intentionally use std::optional so guards can return precise
// diagnostics, matching the established contract style in WP03/WP04 objects.
struct MultiAgentResult {
  // -----------------------------------------------------------------------
  // Required fields (WP04-T016, 3 items)
  // -----------------------------------------------------------------------

  // Per-subtask result references or summaries that justify the merged
  // collaboration output.
  std::optional<std::vector<std::string>> subtask_results;

  // The merged collaboration output after the subtask results have been
  // reconciled by the coordination layer.
  std::optional<std::string> merged_result;

  // Advisory next step for AgentOrchestrator, such as fold, replan, retry,
  // or escalate. This is guidance only, not a terminal result submission.
  std::optional<std::string> recommended_next_action;

  // -----------------------------------------------------------------------
  // Optional fields (WP04-T016, 3 items)
  // -----------------------------------------------------------------------

  // Conflicts or unresolved disagreements observed while reconciling subtask
  // outputs.
  std::optional<std::vector<std::string>> conflicts;

  // Trace or audit references for the worker executions that fed the merged
  // result.
  std::optional<std::vector<std::string>> worker_trace_refs;

  // Collaboration-local failure summary. This is not the top-level ErrorInfo
  // object owned by AgentResult.
  std::optional<std::string> failure_summary;
};

}  // namespace dasall::contracts
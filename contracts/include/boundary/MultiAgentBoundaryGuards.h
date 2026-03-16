#pragma once

#include <array>
#include <string_view>

namespace dasall::contracts {

// MultiAgentBoundaryDecision provides normalized outcomes for multi-agent
// boundary checks so contract tests can assert stable decision semantics.
enum class MultiAgentBoundaryDecision {
  AllowField,
  RejectRequestReuseAgentRequest,
  RejectResultReplaceAgentResult,
  RejectWorkerTaskGlobalState,
};

// MultiAgentBoundaryResult carries a boolean decision plus reason metadata for
// CI-friendly assertions and consistent guard behavior reporting.
struct MultiAgentBoundaryResult {
  bool allowed = true;
  MultiAgentBoundaryDecision decision = MultiAgentBoundaryDecision::AllowField;
  std::string_view reason = "multi-agent boundary field is allowed by ADR-008";
};

// ADR-008 requires MultiAgentRequest to be a collaboration-subdomain request
// and not a reuse wrapper of AgentRequest semantics.
inline constexpr std::array<std::string_view, 2> kMultiAgentRequestForbiddenFields = {
    "agent_request",
    "agent_request_payload",
};

// ADR-008 requires MultiAgentResult to represent collaboration results rather
// than replacing the top-level AgentResult.
inline constexpr std::array<std::string_view, 2> kMultiAgentResultForbiddenFields = {
    "agent_result",
    "final_agent_response",
};

// ADR-008 requires WorkerTask to stay as a subtask execution unit and not
// carry top-level Session/FSM control state.
inline constexpr std::array<std::string_view, 3> kWorkerTaskGlobalStateForbiddenFields = {
    "global_session_state",
    "global_fsm_state",
    "session_fsm_state",
};

// Evaluates candidate field names for MultiAgentRequest.
constexpr MultiAgentBoundaryResult evaluate_multi_agent_request_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kMultiAgentRequestForbiddenFields) {
    if (field_name == forbidden_field) {
      return MultiAgentBoundaryResult{
          .allowed = false,
          .decision = MultiAgentBoundaryDecision::RejectRequestReuseAgentRequest,
          .reason = "multi-agent request must not reuse agent-request semantics",
      };
    }
  }

  return MultiAgentBoundaryResult{};
}

// Evaluates candidate field names for MultiAgentResult.
constexpr MultiAgentBoundaryResult evaluate_multi_agent_result_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kMultiAgentResultForbiddenFields) {
    if (field_name == forbidden_field) {
      return MultiAgentBoundaryResult{
          .allowed = false,
          .decision = MultiAgentBoundaryDecision::RejectResultReplaceAgentResult,
          .reason = "multi-agent result must not replace top-level agent result",
      };
    }
  }

  return MultiAgentBoundaryResult{};
}

// Evaluates candidate field names for WorkerTask.
constexpr MultiAgentBoundaryResult evaluate_worker_task_field_boundary(std::string_view field_name) {
  for (const auto forbidden_field : kWorkerTaskGlobalStateForbiddenFields) {
    if (field_name == forbidden_field) {
      return MultiAgentBoundaryResult{
          .allowed = false,
          .decision = MultiAgentBoundaryDecision::RejectWorkerTaskGlobalState,
          .reason = "worker task must not carry global session or fsm state",
      };
    }
  }

  return MultiAgentBoundaryResult{};
}

// Boolean helpers for callers that only require pass/fail guard semantics.
constexpr bool is_allowed_multi_agent_request_field(std::string_view field_name) {
  return evaluate_multi_agent_request_field_boundary(field_name).allowed;
}

constexpr bool is_allowed_multi_agent_result_field(std::string_view field_name) {
  return evaluate_multi_agent_result_field_boundary(field_name).allowed;
}

constexpr bool is_allowed_worker_task_field(std::string_view field_name) {
  return evaluate_worker_task_field_boundary(field_name).allowed;
}

}  // namespace dasall::contracts

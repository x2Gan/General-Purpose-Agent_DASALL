#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::contracts {

// ---------------------------------------------------------------------------
// ApprovalPolicy enumerates the pre-declared approval stance for a goal,
// as defined by WP03-T004. The Unspecified sentinel follows the WP02-T012
// enum lifecycle rule.
//
// Semantic boundary (WP03-T004 / ADR-007):
//   - ApprovalPolicy expresses *whether* human confirmation is needed before
//     certain goal-level decisions.
//   - It does NOT express recovery details (retry count, backoff, circuit
//     state) — those belong to RecoveryManager (ADR-007).
// ---------------------------------------------------------------------------
enum class ApprovalPolicy {
  Unspecified = 0,    // WP02-T012 sentinel: not yet determined.
  Auto = 1,           // Autonomous execution, no human confirmation needed.
  RequireConfirm = 2, // Critical decisions require human approval first.
};

// ---------------------------------------------------------------------------
// GoalStatus tracks the lifecycle state of a GoalContract instance.
// The Unspecified sentinel follows WP02-T012.
//
// State transitions (managed by AgentOrchestrator only):
//   Unspecified -> Active   (goal extracted from request)
//   Active      -> Achieved (success_criteria satisfied)
//   Active      -> Failed   (explicitly unachievable)
//   Active      -> Cancelled (external cancellation or timeout)
// ---------------------------------------------------------------------------
enum class GoalStatus {
  Unspecified = 0, // WP02-T012 sentinel: not yet initialized.
  Active = 1,      // Goal is currently being pursued.
  Achieved = 2,    // Success criteria have been met.
  Failed = 3,      // Goal is definitively unachievable.
  Cancelled = 4,   // Goal cancelled by external signal or timeout.
};

// ---------------------------------------------------------------------------
// GoalContract is the unified goal contract object for a single Agent task,
// as defined by WP03-T004 semantic boundary.
//
// Five core responsibilities (architecture 3.8.1):
//   1. Goal description:  What the agent should accomplish (What, not How).
//   2. Success criteria:  Measurable completion conditions for Reasoner.
//   3. Constraints:       Inviolable boundary conditions (security/policy).
//   4. Budget declaration: Goal-level budget cap (reuses RuntimeBudget).
//   5. Approval policy:   Pre-declaration of human confirmation needs.
//
// Semantic boundary (WP03-T004 frozen):
//   Allowed:
//     1. Goal semantics: goal_id, request_id, goal_description,
//        success_criteria, status, created_at.
//     2. Constraints: constraints, approval_policy, priority.
//     3. Budget: budget_override, deadline_at.
//     4. Correlation: parent_goal_id, tags.
//
//   Forbidden (WP03-T004 frozen):
//     - Plan/execution steps (plan_graph, step_list, action_sequence)
//     - Runtime internal state (fsm_state, retry_counters, checkpoint_ref)
//     - Provider private fields (rendered_prompt, vendor params)
//     - Execution result state (observation, belief_state, agent_result)
//     - Multi-agent sub-domain fields (worker_task_list, lease_id)
//     - Message assembly fields (final_messages, prompt_bundle, token_usage)
//
// Consumers:
//   - Planner: build_plan(goal, context) — reads goal + criteria + constraints.
//   - Reasoner: evaluates Observation against success_criteria.
//   - ContextOrchestrator: extracts goal summary into ContextPacket.
//   - AgentOrchestrator: manages status lifecycle transitions.
//   - Checkpoint: snapshots GoalContract for recovery.
// ---------------------------------------------------------------------------
struct GoalContract {
  // -----------------------------------------------------------------------
  // Required fields (WP03-T004, 6 items)
  // -----------------------------------------------------------------------

  // Goal-level unique identifier, generated at goal extraction time and
  // propagated throughout the goal lifecycle. References WP02-T009.
  std::optional<std::string> goal_id;

  // Correlation back to the originating AgentRequest.request_id.
  std::optional<std::string> request_id;

  // Concrete goal description derived from AgentRequest.goal_hint and
  // domain context. Expresses *what* the agent should accomplish, never
  // *how* (execution plan is Planner's responsibility).
  std::optional<std::string> goal_description;

  // Measurable success criteria. Must be structured/machine-readable
  // (e.g., key indicators or JSON conditions), not free-form prose.
  // Consumed by Reasoner and ReflectionEngine to determine goal completion.
  std::optional<std::string> success_criteria;

  // Current lifecycle status of this goal. Only AgentOrchestrator may
  // transition this field. References GoalStatus enum.
  std::optional<GoalStatus> status;

  // Goal creation timestamp in milliseconds, serving as temporal baseline.
  // References WP02-T010.
  std::optional<std::int64_t> created_at;

  // -----------------------------------------------------------------------
  // Optional fields (WP03-T004, 7 items)
  // -----------------------------------------------------------------------

  // Declarative constraint set (security/policy/permission boundaries).
  // Inherited from or refined based on AgentRequest.constraint_set.
  // Does not contain execution-state fields.
  std::optional<std::string> constraints;

  // Pre-declared approval policy for goal-level decisions.
  // References ApprovalPolicy enum. Does not contain recovery details
  // (those belong to RecoveryManager per ADR-007).
  std::optional<ApprovalPolicy> approval_policy;

  // Goal-level budget override. If present, takes precedence over
  // AgentRequest.runtime_budget for this goal's scope.
  // Reuses RuntimeBudget structure (WP02-T007), does not invent new dims.
  std::optional<RuntimeBudget> budget_override;

  // Goal priority hint, inherited from AgentRequest.priority.
  // Advisory only, not directly equal to scheduling state.
  std::optional<std::uint32_t> priority;

  // Parent goal identifier for multi-agent hierarchical goal decomposition.
  // Absent (nullopt) in single-agent scenarios.
  std::optional<std::string> parent_goal_id;

  // Goal-level hard deadline timestamp in milliseconds.
  // Inherited from AgentRequest.deadline_at or set by Orchestrator.
  std::optional<std::int64_t> deadline_at;

  // Retrieval/audit tags. Do not carry execution control signals.
  std::optional<std::vector<std::string>> tags;
};

}  // namespace dasall::contracts

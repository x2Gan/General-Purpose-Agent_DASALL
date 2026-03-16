#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

// BoundaryCategory models the three-layer object boundary introduced in WP01.
// Stable objects are allowed in contracts, Blocked objects are prohibited from
// contracts in the current phase, and Deferred objects are phase-gated.
enum class BoundaryCategory {
  Stable,
  Blocked,
  Deferred,
};

// ContractObject enumerates all WP01 object names that must be traceable in
// the boundary catalog. The naming follows the WP01-T006/T007 artifacts.
enum class ContractObject {
  AgentRequest,
  GoalContract,
  ContextPacket,
  ActionDecision,
  Observation,
  WorkerTask,
  ObservationDigest,
  ErrorInfo,
  Checkpoint,
  ReflectionDecision,
  RecoveryOutcome,
  AgentResult,
  MultiAgentRequest,
  MultiAgentResult,
  SessionContextSummary,
  MemoryEvidence,
  KnowledgeEvidence,
  ClarificationHint,
  ReplanHint,
  ExternalAction,
  WorkerResult,
  RetrievalResult,
  HumanInput,
  FinalObservations,
  PlanStatus,
  GoalFragment,
  PlanFragment,
  ToolRequest,
  ToolResult,
};

// ObjectBoundaryEntry is the canonical compile-time row representation used by
// contract tests and by future boundary guards.
struct ObjectBoundaryEntry {
  ContractObject object;
  std::string_view name;
  BoundaryCategory category;
};

inline constexpr std::array<ObjectBoundaryEntry, 29> kObjectBoundaryCatalog = {{
    {ContractObject::AgentRequest, "AgentRequest", BoundaryCategory::Stable},
    {ContractObject::GoalContract, "GoalContract", BoundaryCategory::Stable},
    {ContractObject::ContextPacket, "ContextPacket", BoundaryCategory::Stable},
    {ContractObject::ActionDecision, "ActionDecision", BoundaryCategory::Stable},
    {ContractObject::Observation, "Observation", BoundaryCategory::Stable},
    {ContractObject::WorkerTask, "WorkerTask", BoundaryCategory::Stable},
    {ContractObject::ObservationDigest, "ObservationDigest", BoundaryCategory::Stable},
    {ContractObject::ErrorInfo, "ErrorInfo", BoundaryCategory::Stable},
    {ContractObject::Checkpoint, "Checkpoint", BoundaryCategory::Stable},
    {ContractObject::ReflectionDecision, "ReflectionDecision", BoundaryCategory::Stable},
    {ContractObject::RecoveryOutcome, "RecoveryOutcome", BoundaryCategory::Stable},
    {ContractObject::AgentResult, "AgentResult", BoundaryCategory::Stable},
    {ContractObject::MultiAgentRequest, "MultiAgentRequest", BoundaryCategory::Stable},
    {ContractObject::MultiAgentResult, "MultiAgentResult", BoundaryCategory::Stable},
    {ContractObject::SessionContextSummary, "SessionContextSummary", BoundaryCategory::Blocked},
    {ContractObject::MemoryEvidence, "MemoryEvidence", BoundaryCategory::Blocked},
    {ContractObject::KnowledgeEvidence, "KnowledgeEvidence", BoundaryCategory::Blocked},
    {ContractObject::ClarificationHint, "ClarificationHint", BoundaryCategory::Blocked},
    {ContractObject::ReplanHint, "ReplanHint", BoundaryCategory::Blocked},
    {ContractObject::ExternalAction, "ExternalAction", BoundaryCategory::Blocked},
    {ContractObject::WorkerResult, "WorkerResult", BoundaryCategory::Blocked},
    {ContractObject::RetrievalResult, "RetrievalResult", BoundaryCategory::Blocked},
    {ContractObject::HumanInput, "HumanInput", BoundaryCategory::Blocked},
    {ContractObject::FinalObservations, "FinalObservations", BoundaryCategory::Blocked},
    {ContractObject::PlanStatus, "PlanStatus", BoundaryCategory::Blocked},
    {ContractObject::GoalFragment, "GoalFragment", BoundaryCategory::Blocked},
    {ContractObject::PlanFragment, "PlanFragment", BoundaryCategory::Blocked},
    {ContractObject::ToolRequest, "ToolRequest", BoundaryCategory::Deferred},
    {ContractObject::ToolResult, "ToolResult", BoundaryCategory::Deferred},
}};

// Returns the boundary category for a catalog object. The default return path
// is intentionally unreachable for valid enum values and exists as a fallback
// for defensive compatibility with future enum extension.
constexpr BoundaryCategory boundary_category(ContractObject object) {
  for (const auto& entry : kObjectBoundaryCatalog) {
    if (entry.object == object) {
      return entry.category;
    }
  }

  return BoundaryCategory::Blocked;
}

// Returns the canonical object name captured from WP01 artifacts.
constexpr std::string_view object_name(ContractObject object) {
  for (const auto& entry : kObjectBoundaryCatalog) {
    if (entry.object == object) {
      return entry.name;
    }
  }

  return "UnknownObject";
}

constexpr std::size_t count_by_category(BoundaryCategory category) {
  std::size_t count = 0;
  for (const auto& entry : kObjectBoundaryCatalog) {
    if (entry.category == category) {
      ++count;
    }
  }

  return count;
}

constexpr bool is_stable_object(ContractObject object) {
  return boundary_category(object) == BoundaryCategory::Stable;
}

constexpr bool is_blocked_object(ContractObject object) {
  return boundary_category(object) == BoundaryCategory::Blocked;
}

constexpr bool is_deferred_object(ContractObject object) {
  return boundary_category(object) == BoundaryCategory::Deferred;
}

}  // namespace dasall::contracts

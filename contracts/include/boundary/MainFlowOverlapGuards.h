// ==========================================================================
// MainFlowOverlapGuards.h
//
// WP03-T016-B: Compile-time responsibility overlap guards for the 8 main
// flow contract objects.  Ensures no object carries fields that belong to
// Prompt, Recovery, Worker, Runtime-internal, or Provider domains.
//
// Design basis:
//   - WP03-T016-D 职责重叠检查单 (8×5 互斥矩阵)
//   - ADR-006 (ContextPacket ≠ Prompt domain)
//   - ADR-007 (Checkpoint ≠ Recovery attribution domain)
//   - ADR-008 (Main-flow ≠ Worker sub-domain)
//   - T002-T014 per-object forbidden field specifications
//
// Guard pattern: field-name whitelist/blacklist per object, following the
// same evaluate_*_field_boundary / is_allowed_*_field dual-interface
// convention established by ContextBoundaryGuards.h and friends.
//
// All functions are constexpr / inline to keep header-only, zero-runtime-
// cost characteristics.
// ==========================================================================
#pragma once

#include <array>
#include <string_view>

namespace dasall::contracts {

// -----------------------------------------------------------------------
// 1. Decision / Result types
// -----------------------------------------------------------------------

/// Enumeration of overlap rejection categories corresponding to the five
/// forbidden domains identified in the T016-D 8×5 matrix.
enum class MainFlowOverlapDecision {
  AllowField,                     // 字段合法，不属于任何禁止域
  RejectPromptDomain,             // 字段属于 Prompt/消息渲染域
  RejectRecoveryDomain,           // 字段属于 Recovery/恢复执行域
  RejectWorkerDomain,             // 字段属于 Worker/多 Agent 子域
  RejectRuntimeInternalDomain,    // 字段属于 Runtime 内部状态域
  RejectProviderDomain,           // 字段属于 Provider/供应商私有域
};

/// Guard result carrying binary outcome, decision code, and human-readable
/// reason for CI/test assertion consumption.
struct MainFlowOverlapResult {
  bool allowed = true;
  MainFlowOverlapDecision decision = MainFlowOverlapDecision::AllowField;
  std::string_view object_name = "";
  std::string_view reason = "field is allowed by main-flow overlap guard";
};

// -----------------------------------------------------------------------
// 2. Per-domain forbidden field arrays (union across all 8 objects)
//
// These represent the union set of fields that must NEVER appear in ANY
// of the 8 main-flow objects.  Individual per-object guards can be added
// later if finer granularity is needed; at T016 scope the union approach
// matches the 8×5 matrix design.
// -----------------------------------------------------------------------

/// 2.1 Prompt domain forbidden fields
///     Source: ADR-006 §3.3 + T002-T014 禁止字段表
inline constexpr std::array<std::string_view, 4> kPromptDomainForbiddenFields = {
    "final_messages",       // 消息渲染输出 (ADR-006)
    "rendered_prompt",      // 提示词渲染结果 (ADR-006)
    "provider_payload",     // 供应商消息载荷 (ADR-006)
    "prompt_bundle",        // 提示词包 (BeliefState 禁止)
};

/// 2.2 Recovery domain forbidden fields
///     Source: ADR-007 §5.1/§5.3 + T012-T014 禁止字段表
inline constexpr std::array<std::string_view, 5> kRecoveryDomainForbiddenFields = {
    "retry_after_ms",               // 调度延迟 (ADR-007)
    "backoff_strategy",             // 退避策略 (ADR-007)
    "circuit_breaker_transition",   // 熔断器转换 (ADR-007)
    "failure_root_cause",           // 故障归因 (ADR-007, Recovery 专属)
    "root_cause_analysis",          // 根因分析 (ADR-007, Recovery 专属)
};

/// 2.3 Worker / multi-agent sub-domain forbidden fields
///     Source: ADR-008 §5.1-§5.3 + T002/T014 禁止字段表
inline constexpr std::array<std::string_view, 5> kWorkerDomainForbiddenFields = {
    "worker_task_id",           // Worker 子任务 ID
    "worker_results",           // Worker 结果集
    "worker_lease",             // Worker 租约
    "multi_agent_mode",         // 多 Agent 模式标志
    "merged_result",            // 合并结果 (AgentResult 禁止)
};

/// 2.4 Runtime-internal domain forbidden fields
///     Source: T002-T014 禁止字段表 (非 Recovery 的运行时内部状态)
inline constexpr std::array<std::string_view, 5> kRuntimeInternalForbiddenFields = {
    "fsm_state",                // 有限状态机内部状态
    "current_step",             // 当前步骤指针
    "plan_graph",               // 计划图 (GoalContract 禁止)
    "internal_checkpoint",      // 内部检查点 (AgentResult 禁止)
    "observation_history",      // 观测历史 (AgentResult 禁止)
};

/// 2.5 Provider domain forbidden fields
///     Source: T002-T014 禁止字段表
inline constexpr std::array<std::string_view, 3> kProviderDomainForbiddenFields = {
    "model_selection",          // 模型选择 (AgentRequest 禁止)
    "vendor_tool_schema",       // 供应商工具 Schema (GoalContract 禁止)
    "model_provider_args",      // 供应商参数 (GoalContract 禁止)
};

// -----------------------------------------------------------------------
// 3. Core evaluation function
//
// Checks a candidate field name against all 5 forbidden-domain arrays.
// Returns the first matching rejection, or AllowField if no match.
// -----------------------------------------------------------------------

/// Evaluate whether a field name is allowed in any main-flow object.
/// The `object_name` parameter is carried through to the result for
/// auditing / logging purposes but does not alter the evaluation logic.
constexpr MainFlowOverlapResult evaluate_main_flow_overlap(
    std::string_view object_name,
    std::string_view field_name) {

  // --- Prompt domain ---
  for (const auto& f : kPromptDomainForbiddenFields) {
    if (field_name == f) {
      return MainFlowOverlapResult{
          .allowed = false,
          .decision = MainFlowOverlapDecision::RejectPromptDomain,
          .object_name = object_name,
          .reason = "main-flow object must not contain prompt/message-rendering fields",
      };
    }
  }

  // --- Recovery domain ---
  for (const auto& f : kRecoveryDomainForbiddenFields) {
    if (field_name == f) {
      return MainFlowOverlapResult{
          .allowed = false,
          .decision = MainFlowOverlapDecision::RejectRecoveryDomain,
          .object_name = object_name,
          .reason = "main-flow object must not contain recovery-execution scheduling fields",
      };
    }
  }

  // --- Worker domain ---
  for (const auto& f : kWorkerDomainForbiddenFields) {
    if (field_name == f) {
      return MainFlowOverlapResult{
          .allowed = false,
          .decision = MainFlowOverlapDecision::RejectWorkerDomain,
          .object_name = object_name,
          .reason = "main-flow object must not contain worker/multi-agent sub-domain fields",
      };
    }
  }

  // --- Runtime-internal domain ---
  for (const auto& f : kRuntimeInternalForbiddenFields) {
    if (field_name == f) {
      return MainFlowOverlapResult{
          .allowed = false,
          .decision = MainFlowOverlapDecision::RejectRuntimeInternalDomain,
          .object_name = object_name,
          .reason = "main-flow object must not contain runtime-internal state fields",
      };
    }
  }

  // --- Provider domain ---
  for (const auto& f : kProviderDomainForbiddenFields) {
    if (field_name == f) {
      return MainFlowOverlapResult{
          .allowed = false,
          .decision = MainFlowOverlapDecision::RejectProviderDomain,
          .object_name = object_name,
          .reason = "main-flow object must not contain provider/vendor-specific fields",
      };
    }
  }

  return MainFlowOverlapResult{
      .allowed = true,
      .decision = MainFlowOverlapDecision::AllowField,
      .object_name = object_name,
      .reason = "field is allowed by main-flow overlap guard",
  };
}

// -----------------------------------------------------------------------
// 4. Boolean helper (pass/fail only)
// -----------------------------------------------------------------------

/// Convenience function returning true if the field is allowed.
constexpr bool is_allowed_main_flow_field(
    std::string_view object_name,
    std::string_view field_name) {
  return evaluate_main_flow_overlap(object_name, field_name).allowed;
}

// -----------------------------------------------------------------------
// 5. Symmetric exclusion helpers for adjacent object pairs
//
// These functions verify that a field name belongs exclusively to one
// object in a pair and is NOT a valid required-field of the other.
// They encode the 4 adjacent-pair rules from T016-D §3.
// -----------------------------------------------------------------------

/// 5.1 Observation-only fields that must NOT appear in ObservationDigest.
inline constexpr std::array<std::string_view, 6> kObservationOnlyFields = {
    "payload", "success", "error", "side_effects", "tool_call_id", "duration_ms",
};

/// 5.2 ObservationDigest-only fields that must NOT appear in Observation.
inline constexpr std::array<std::string_view, 5> kDigestOnlyFields = {
    "summary", "key_facts", "citations", "confidence", "omitted_details",
};

/// 5.3 BeliefState-only fields that must NOT appear in Checkpoint.
inline constexpr std::array<std::string_view, 6> kBeliefStateOnlyFields = {
    "confirmed_facts", "hypotheses", "assumptions",
    "evidence_refs", "active_plan_summary", "unresolved_questions",
};

/// 5.4 Checkpoint-only fields that must NOT appear in BeliefState.
inline constexpr std::array<std::string_view, 5> kCheckpointOnlyFields = {
    "state", "step_id", "working_memory_snapshot", "pending_action", "retry_count",
};

/// Check that a field intended for Observation is not a Digest-only field.
constexpr bool is_observation_not_digest_field(std::string_view field_name) {
  for (const auto& f : kDigestOnlyFields) {
    if (field_name == f) return false;
  }
  return true;
}

/// Check that a field intended for ObservationDigest is not an Observation-only field.
constexpr bool is_digest_not_observation_field(std::string_view field_name) {
  for (const auto& f : kObservationOnlyFields) {
    if (field_name == f) return false;
  }
  return true;
}

/// Check that a field intended for BeliefState is not a Checkpoint-only field.
constexpr bool is_belief_state_not_checkpoint_field(std::string_view field_name) {
  for (const auto& f : kCheckpointOnlyFields) {
    if (field_name == f) return false;
  }
  return true;
}

/// Check that a field intended for Checkpoint is not a BeliefState-only field.
constexpr bool is_checkpoint_not_belief_state_field(std::string_view field_name) {
  for (const auto& f : kBeliefStateOnlyFields) {
    if (field_name == f) return false;
  }
  return true;
}

// -----------------------------------------------------------------------
// 6. Domain-ownership constants (T016-D §4)
// -----------------------------------------------------------------------

/// Canonical domain label for each of the 8 main-flow objects.
inline constexpr std::array<std::string_view, 8> kMainFlowObjectNames = {
    "AgentRequest", "GoalContract", "ContextPacket", "Observation",
    "ObservationDigest", "BeliefState", "Checkpoint", "AgentResult",
};

inline constexpr std::array<std::string_view, 8> kMainFlowObjectDomains = {
    "AccessLayer",       // AgentRequest
    "ControlPlane",      // GoalContract
    "MemoryLayer",       // ContextPacket
    "ExecutionLayer",    // Observation
    "CognitionLayer",    // ObservationDigest
    "CognitionLayer",    // BeliefState
    "RuntimeLayer",      // Checkpoint
    "AccessLayer",       // AgentResult
};

/// Verify that each object has exactly one domain assignment (compile-time
/// sanity check — always true by construction; test asserts count == 8).
constexpr bool verify_domain_assignment_completeness() {
  return kMainFlowObjectNames.size() == 8 &&
         kMainFlowObjectDomains.size() == 8;
}

}  // namespace dasall::contracts

// ==========================================================================
// PromptBoundaryContracts.h
//
// WP04-T001-B: Prompt boundary contracts aggregate entry point.
//
// This header is the single include for the Prompt subdomain boundary
// guards introduced in WP04.  It centralizes the three ADR-006-derived
// boundary decisions for:
//   1. ContextPacket — must NOT carry message-rendering-layer fields.
//   2. PromptComposeRequest — must NOT become a second ContextPacket owner.
//   3. PromptComposeResult  — must NOT write back to memory/context.
//
// Design basis (WP04-T001-D):
//   - ADR-006 §6.1 / §8: ContextPacket message-layer forbidden fields
//   - ADR-006 §6.2 / §7 (option B rejected): PromptComposeRequest context-
//     ownership forbidden fields
//   - ADR-006 §3.3 / §6.3: PromptComposeResult memory-writeback forbidden
//     fields
//
// Pattern: header-only, constexpr arrays + inline evaluate functions.
// Follows dasall::contracts boundary guard conventions established by
// RecoveryBoundaryGuards.h and MultiAgentBoundaryGuards.h.
//
// Consumers:
//   - tests/contract/smoke/PromptBoundaryContractsSmokeTest.cpp (T001-B)
//   - Future WP04 contract tests: T002-B through T005-B (Prompt objects)
// ==========================================================================
#pragma once

#include <array>
#include <string_view>

namespace dasall::contracts {

// --------------------------------------------------------------------------
// 1. Decision enum
//    Maps every possible boundary violation to a stable, enumerated outcome.
//    Allows contract tests to assert exact decision codes, not just booleans.
// --------------------------------------------------------------------------

/// PromptBoundaryDecision encodes the three distinct ADR-006 boundary
/// violation categories that Prompt subdomain objects must never cross.
enum class PromptBoundaryDecision {
  /// The evaluated field satisfies all ADR-006 Prompt boundary constraints.
  AllowField,

  /// The field attempts to introduce message-rendering semantics (e.g.
  /// final_messages, rendered_prompt) into ContextPacket, which must carry
  /// only semantic-context content.  ADR-006 §6.1 / §8.
  RejectContextPacketMessageField,

  /// The field attempts to give PromptComposeRequest direct ownership of
  /// raw context assembly data (e.g. memory_snapshot, retrieval_candidates),
  /// which would make it a second ContextPacket and violate the single
  /// context-owner invariant.  ADR-006 §6.2 / §7 (option B rejected).
  RejectComposeRequestContextOwnership,

  /// The field attempts to add memory / context write-back semantics (e.g.
  /// memory_write_back, belief_patch) to PromptComposeResult, whose sole
  /// responsibility is expressing assembly output and metadata.
  /// ADR-006 §3.3 / §6.3.
  RejectComposeResultMemoryWriteback,
};

// --------------------------------------------------------------------------
// 2. Result struct
//    Carries all information needed for CI assertions and human-readable
//    diagnostics in a single, zero-allocation value type.
// --------------------------------------------------------------------------

/// PromptBoundaryResult is the unified guard evaluation outcome reported by
/// all three evaluate_* functions in this header.
struct PromptBoundaryResult {
  /// true when no boundary violation was detected for the evaluated field.
  bool allowed = true;

  /// Stable decision code; always AllowField when allowed == true.
  PromptBoundaryDecision decision = PromptBoundaryDecision::AllowField;

  /// Human-readable explanation; non-empty when allowed == false.
  std::string_view reason = "prompt boundary field is allowed by ADR-006";
};

// --------------------------------------------------------------------------
// 3. Forbidden field tables (ADR-006 §6.1 / §6.2 / §3.3)
//
// Each array is intentionally minimal: it captures the canonical examples
// named explicitly in ADR-006.  Derived or vendor-specific variants are
// caught at the semantic level by the evaluate_* functions via exact-name
// lookup; this design matches the established pattern in
// RecoveryBoundaryGuards.h and MultiAgentBoundaryGuards.h.
// --------------------------------------------------------------------------

/// Fields that ADR-006 §6.1/§8 explicitly forbid inside ContextPacket.
/// These belong to the message-rendering / prompt-assembly layer and must
/// live in PromptComposeResult or LLMAdapter, never in ContextPacket.
inline constexpr std::array<std::string_view, 8> kContextPacketPromptForbiddenFields = {
    "final_messages",      // ADR-006 §6.1 禁令 — PromptComposeResult 产出
    "provider_payload",    // ADR-006 §6.1 禁令 — 模型厂商格式，非语义上下文
    "rendered_prompt",     // ADR-006 §6.1 禁令 — 消息渲染产物
    "prompt_bundle",       // ADR-006 §6.1 禁令 — Prompt 装配包
    "system_instructions", // ADR-006 §3.3 条款 — Prompt 模板注入，归 PromptComposer
    "few_shots",           // ADR-006 §3.3 条款 — 示例注入，归 PromptComposer
    "output_schema",       // ADR-006 §3.3 条款 — 输出约束，归 PromptComposer
    "tool_schemas",        // ADR-006 §3.4 条款 — 工具定义，归 PromptComposer/PromptPolicy
};

/// Fields that ADR-006 §6.2/§7 forbid inside PromptComposeRequest.
/// Including any of these would promote PromptComposeRequest to a second
/// semantic-context owner, violating the single-owner invariant.
inline constexpr std::array<std::string_view, 4> kComposeRequestContextOwnershipForbiddenFields = {
    "memory_snapshot",            // ADR-006 §3.2 / §7 — WorkingMemory 内容归 ContextOrchestrator
    "retrieval_candidates",       // ADR-006 §3.2 / §3.3 — 候选片段检索归 ContextOrchestrator
    "context_packet_internal",    // ADR-006 §6.1 — ContextPacket 内部数据不透传给装配请求
    "knowledge_fragments",        // ADR-006 §3.3 条款 1 — 未处理的知识片段归 ContextOrchestrator
};

/// Fields that ADR-006 §3.3/§6.3 forbid inside PromptComposeResult.
/// Including any of these would give PromptComposer a back-channel write
/// path into memory, which must remain exclusively in the memory subsystem.
inline constexpr std::array<std::string_view, 5> kComposeResultMemoryWritebackForbiddenFields = {
    "memory_write_back",  // ADR-006 §3.3 条款 5 — PromptComposer 不得写回 memory
    "context_update",     // ADR-006 §3.2 — ContextPacket 更新由 ContextOrchestrator 负责
    "belief_patch",       // ADR-006 §3.3 条款 3 / §7 — BeliefState 修订由 Cognition/memory 负责
    "knowledge_recall",   // ADR-006 §3.3 条款 1 — 知识召回由 ContextOrchestrator 发起
    "history_update",     // ADR-006 §3.3 条款 3 — 历史记录写回归 memory 子系统
};

// --------------------------------------------------------------------------
// 4. Boundary evaluation functions
//    All three functions follow the same constexpr lookup pattern:
//      1. Iterate over the relevant forbidden-field array.
//      2. Return a reject result on first match.
//      3. Return a default AllowField result when no match is found.
// --------------------------------------------------------------------------

/// Evaluates whether a candidate field name is legal inside ContextPacket
/// from the Prompt boundary perspective.
///
/// A field is illegal when it belongs to the message-rendering / prompt-
/// assembly layer (ADR-006 §6.1/§8).  Such fields must live in
/// PromptComposeResult, not in the semantic-context carrier.
///
/// @param field_name  Exact field name string (case-sensitive).
/// @return            PromptBoundaryResult with allowed == false and
///                    decision == RejectContextPacketMessageField when
///                    the field violates the boundary.
constexpr PromptBoundaryResult evaluate_context_packet_prompt_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden : kContextPacketPromptForbiddenFields) {
    if (field_name == forbidden) {
      return PromptBoundaryResult{
          .allowed  = false,
          .decision = PromptBoundaryDecision::RejectContextPacketMessageField,
          .reason   = "context packet must not contain message-rendering-layer fields (ADR-006 §6.1)",
      };
    }
  }
  return PromptBoundaryResult{};
}

/// Evaluates whether a candidate field name is legal inside
/// PromptComposeRequest from the context-ownership perspective.
///
/// A field is illegal when it would make PromptComposeRequest an owner of
/// raw context assembly data, which belongs exclusively to ContextOrchestrator
/// (ADR-006 §6.2 / §7 option B rejected).
///
/// @param field_name  Exact field name string (case-sensitive).
/// @return            PromptBoundaryResult with allowed == false and
///                    decision == RejectComposeRequestContextOwnership when
///                    the field violates the boundary.
constexpr PromptBoundaryResult evaluate_compose_request_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden : kComposeRequestContextOwnershipForbiddenFields) {
    if (field_name == forbidden) {
      return PromptBoundaryResult{
          .allowed  = false,
          .decision = PromptBoundaryDecision::RejectComposeRequestContextOwnership,
          .reason   = "compose request must not own context assembly data (ADR-006 §6.2)",
      };
    }
  }
  return PromptBoundaryResult{};
}

/// Evaluates whether a candidate field name is legal inside
/// PromptComposeResult from the memory-writeback perspective.
///
/// A field is illegal when it introduces a write-back path into
/// memory/context, which must remain in the memory subsystem
/// (ADR-006 §3.3 / §6.3).
///
/// @param field_name  Exact field name string (case-sensitive).
/// @return            PromptBoundaryResult with allowed == false and
///                    decision == RejectComposeResultMemoryWriteback when
///                    the field violates the boundary.
constexpr PromptBoundaryResult evaluate_compose_result_field_boundary(
    std::string_view field_name) {
  for (const auto forbidden : kComposeResultMemoryWritebackForbiddenFields) {
    if (field_name == forbidden) {
      return PromptBoundaryResult{
          .allowed  = false,
          .decision = PromptBoundaryDecision::RejectComposeResultMemoryWriteback,
          .reason   = "compose result must not contain memory write-back fields (ADR-006 §3.3)",
      };
    }
  }
  return PromptBoundaryResult{};
}

// --------------------------------------------------------------------------
// 5. Boolean shortcut helpers
//    Provided for callers that only need pass/fail semantics and do not
//    need to inspect the decision code or reason string.
// --------------------------------------------------------------------------

/// Returns true when field_name is legal in ContextPacket (Prompt boundary).
constexpr bool is_allowed_context_packet_prompt_field(std::string_view field_name) {
  return evaluate_context_packet_prompt_field_boundary(field_name).allowed;
}

/// Returns true when field_name is legal in PromptComposeRequest.
constexpr bool is_allowed_compose_request_field(std::string_view field_name) {
  return evaluate_compose_request_field_boundary(field_name).allowed;
}

/// Returns true when field_name is legal in PromptComposeResult.
constexpr bool is_allowed_compose_result_field(std::string_view field_name) {
  return evaluate_compose_result_field_boundary(field_name).allowed;
}

}  // namespace dasall::contracts

// ==========================================================================
// M3ChecklistGuards.h
//
// WP03-T017-B: M3 milestone checklist guards for the main flow contract
// freeze.  Encodes the 10 review-conclusion gates from WP03-T017-D into
// a programmatically verifiable checklist, following the same pattern as
// M2ChecklistGuards.h.
//
// Design basis:
//   - WP03-T017-D 评审纪要 §5 (M3 冻结进阶条件清单, 10 Gate)
//   - M2ChecklistGuards.h (gate-input + validate pattern)
//   - T015-D: 端到端流转完整性 (8 nodes, 8 edges)
//   - T016-D: 职责重叠互斥矩阵 (8×5 matrix)
//   - ADR-006/007/008: 高扇出对象一致性
//
// Gate definitions (from T017-D §5):
//   G1  — 8 对象必填字段 Guard 全通过
//   G2  — 5 域禁止字段覆盖 (Prompt/Recovery/Worker/Runtime/Provider)
//   G3  — 4 组相邻对象互斥 (Obs↔Digest, BeliefState↔Checkpoint)
//   G4  — ADR-006 一致性 (ContextPacket ≠ Prompt 域)
//   G5  — ADR-007 一致性 (Checkpoint ≠ Recovery 域)
//   G6  — ADR-008 一致性 (AgentResult ≠ Worker 域)
//   G7  — 端到端关联完整 (8 条关联边)
//   G8  — 域归属唯一 (8 对象 → 确定域)
//   G9  — Contract test 100% 通过
//   G10 — M2 checklist 已通过 (前置里程碑)
//
// All functions are inline to keep header-only, zero-runtime-cost
// characteristics consistent with the existing guard framework.
// ==========================================================================
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

// -----------------------------------------------------------------------
// 1. Gate count constant
// -----------------------------------------------------------------------

/// Total number of M3 checklist gates (must match T017-D §5 gate count).
inline constexpr std::size_t kM3GateCount = 10;

// -----------------------------------------------------------------------
// 2. Input struct — one boolean per gate
// -----------------------------------------------------------------------

/// M3ChecklistInputs is the executable projection of WP03-T017-D §5.
/// Each boolean represents whether one review gate has been verified.
struct M3ChecklistInputs {
  bool gate_g1_required_field_guards   = false;  // G1: 8 对象必填字段 Guard 全通过
  bool gate_g2_forbidden_domain_cover  = false;  // G2: 5 域禁止字段覆盖
  bool gate_g3_symmetric_exclusion     = false;  // G3: 4 组相邻对象互斥
  bool gate_g4_adr006_context_packet   = false;  // G4: ADR-006 一致性
  bool gate_g5_adr007_checkpoint       = false;  // G5: ADR-007 一致性
  bool gate_g6_adr008_agent_result     = false;  // G6: ADR-008 一致性
  bool gate_g7_e2e_correlation         = false;  // G7: 端到端关联完整
  bool gate_g8_domain_uniqueness       = false;  // G8: 域归属唯一
  bool gate_g9_contract_tests_passed   = false;  // G9: Contract test 100% 通过
  bool gate_g10_m2_checklist_passed    = false;  // G10: M2 checklist 已通过
};

// -----------------------------------------------------------------------
// 3. Result struct — per-gate status + first failed gate
// -----------------------------------------------------------------------

/// M3ChecklistResult carries the aggregate and per-gate outcome for
/// audit / CI consumption.
struct M3ChecklistResult {
  bool ok = false;

  // Per-gate pass/fail mirror (for structured reporting).
  bool gate_g1_passed  = false;
  bool gate_g2_passed  = false;
  bool gate_g3_passed  = false;
  bool gate_g4_passed  = false;
  bool gate_g5_passed  = false;
  bool gate_g6_passed  = false;
  bool gate_g7_passed  = false;
  bool gate_g8_passed  = false;
  bool gate_g9_passed  = false;
  bool gate_g10_passed = false;

  /// Identifier of the first gate that failed, or "none" if all passed.
  std::string_view first_failed_gate = "none";

  /// Human-readable reason for CI / test assertion output.
  std::string_view reason = "M3 checklist validation not yet run";
};

// -----------------------------------------------------------------------
// 4. Gate name / description arrays (audit-friendly)
// -----------------------------------------------------------------------

/// Short identifiers for each gate (G1 through G10).
inline constexpr std::array<std::string_view, kM3GateCount> kM3GateNames = {
    "G1", "G2", "G3", "G4", "G5",
    "G6", "G7", "G8", "G9", "G10",
};

/// Human-readable descriptions for each gate.
inline constexpr std::array<std::string_view, kM3GateCount> kM3GateDescriptions = {
    "8 objects required-field guards pass",              // G1
    "5 forbidden domains covered (8x5 matrix)",          // G2
    "4 symmetric exclusion pairs verified",              // G3
    "ADR-006: ContextPacket != Prompt domain",           // G4
    "ADR-007: Checkpoint != Recovery domain",            // G5
    "ADR-008: AgentResult != Worker domain",             // G6
    "End-to-end correlation (8 edges) intact",           // G7
    "Domain assignment unique for 8 objects",            // G8
    "Contract tests 100% passed",                        // G9
    "M2 checklist prerequisite passed",                  // G10
};

// -----------------------------------------------------------------------
// 5. Helper: extract gate values as fixed-size array
// -----------------------------------------------------------------------

/// Returns gate boolean values in G1-G10 order for indexed iteration.
inline std::array<bool, kM3GateCount> m3_checklist_gate_values(
    const M3ChecklistInputs& inputs) {
  return {
      inputs.gate_g1_required_field_guards,
      inputs.gate_g2_forbidden_domain_cover,
      inputs.gate_g3_symmetric_exclusion,
      inputs.gate_g4_adr006_context_packet,
      inputs.gate_g5_adr007_checkpoint,
      inputs.gate_g6_adr008_agent_result,
      inputs.gate_g7_e2e_correlation,
      inputs.gate_g8_domain_uniqueness,
      inputs.gate_g9_contract_tests_passed,
      inputs.gate_g10_m2_checklist_passed,
  };
}

// -----------------------------------------------------------------------
// 6. Core validation function
// -----------------------------------------------------------------------

/// Validates WP-03 M3 milestone readiness by requiring all 10 checklist
/// gates (G1-G10) to pass.  Returns a structured result with per-gate
/// status and the first failed gate identifier for deterministic debugging.
///
/// Usage example (CI / test):
///   M3ChecklistInputs inputs{...};
///   auto result = validate_m3_checklist(inputs);
///   assert(result.ok);
inline M3ChecklistResult validate_m3_checklist(const M3ChecklistInputs& inputs) {
  const auto gate_values = m3_checklist_gate_values(inputs);

  // Scan gates in strict G1-G10 order; report first failure.
  for (std::size_t i = 0; i < kM3GateCount; ++i) {
    if (!gate_values[i]) {
      return M3ChecklistResult{
          .ok             = false,
          .gate_g1_passed  = inputs.gate_g1_required_field_guards,
          .gate_g2_passed  = inputs.gate_g2_forbidden_domain_cover,
          .gate_g3_passed  = inputs.gate_g3_symmetric_exclusion,
          .gate_g4_passed  = inputs.gate_g4_adr006_context_packet,
          .gate_g5_passed  = inputs.gate_g5_adr007_checkpoint,
          .gate_g6_passed  = inputs.gate_g6_adr008_agent_result,
          .gate_g7_passed  = inputs.gate_g7_e2e_correlation,
          .gate_g8_passed  = inputs.gate_g8_domain_uniqueness,
          .gate_g9_passed  = inputs.gate_g9_contract_tests_passed,
          .gate_g10_passed = inputs.gate_g10_m2_checklist_passed,
          .first_failed_gate = kM3GateNames[i],
          .reason = "M3 checklist contains failed gate — WP-03 freeze blocked",
      };
    }
  }

  // All gates passed — M3 freeze ready.
  return M3ChecklistResult{
      .ok             = true,
      .gate_g1_passed  = true,
      .gate_g2_passed  = true,
      .gate_g3_passed  = true,
      .gate_g4_passed  = true,
      .gate_g5_passed  = true,
      .gate_g6_passed  = true,
      .gate_g7_passed  = true,
      .gate_g8_passed  = true,
      .gate_g9_passed  = true,
      .gate_g10_passed = true,
      .first_failed_gate = "none",
      .reason = "M3 checklist passed — WP-03 freeze ready",
  };
}

// -----------------------------------------------------------------------
// 7. Convenience: count passed / failed gates
// -----------------------------------------------------------------------

/// Returns the number of gates that passed (0-10).
inline std::size_t m3_count_passed_gates(const M3ChecklistInputs& inputs) {
  const auto vals = m3_checklist_gate_values(inputs);
  std::size_t count = 0;
  for (bool v : vals) {
    if (v) ++count;
  }
  return count;
}

}  // namespace dasall::contracts

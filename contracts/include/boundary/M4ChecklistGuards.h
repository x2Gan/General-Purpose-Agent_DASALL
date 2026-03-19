// ============================================================================
// M4ChecklistGuards.h
//
// WP04-T023-B: M4 milestone checklist guards for the boundary-object freeze.
// Encodes the 10 review-conclusion gates from WP04-T023-D into a compact,
// programmatically verifiable checklist. The implementation intentionally stays
// header-only and boolean-driven so later gates can consume it without pulling
// in runtime behavior or revalidating field catalogs.
//
// Design basis:
//   - WP04-T023-D 评审纪要 §5 (M4 冻结前评审结论表, 10 Gate)
//   - WP04-T022-D/B (10 objects + 57 forbidden-field mappings)
//   - M3ChecklistGuards.h (existing review-checklist pattern)
//   - ADR-006 / ADR-007 / ADR-008 single-owner boundaries
//
// Gate definitions (from T023-D §5):
//   G1  — 10 objects reviewed and frozen
//   G2  — T022 object catalog complete (3/3/4)
//   G3  — T022 forbidden-field catalog complete and guard-dispatched (57)
//   G4  — ADR-006 keeps a single context owner
//   G5  — ADR-007 keeps a single recovery owner
//   G6  — ADR-008 keeps a single global owner
//   G7  — Worker-domain layering does not absorb top-level semantics
//   G8  — Review conclusions are fully programmable as checklist gates
//   G9  — Contract tests pass for the boundary-freeze milestone
//   G10 — T022 ADR mapping checklist already passes
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

inline constexpr std::size_t kM4GateCount = 10;

struct M4ChecklistInputs {
  bool gate_g1_review_scope_complete = false;
  bool gate_g2_object_catalog_complete = false;
  bool gate_g3_forbidden_field_catalog_complete = false;
  bool gate_g4_adr006_single_context_owner = false;
  bool gate_g5_adr007_single_recovery_owner = false;
  bool gate_g6_adr008_single_global_owner = false;
  bool gate_g7_worker_domain_layering = false;
  bool gate_g8_checklist_programmable = false;
  bool gate_g9_contract_tests_passed = false;
  bool gate_g10_t022_mapping_passed = false;
};

struct M4ChecklistResult {
  bool ok = false;

  bool gate_g1_passed = false;
  bool gate_g2_passed = false;
  bool gate_g3_passed = false;
  bool gate_g4_passed = false;
  bool gate_g5_passed = false;
  bool gate_g6_passed = false;
  bool gate_g7_passed = false;
  bool gate_g8_passed = false;
  bool gate_g9_passed = false;
  bool gate_g10_passed = false;

  std::string_view first_failed_gate = "none";
  std::string_view reason = "M4 checklist validation not yet run";
};

inline constexpr std::array<std::string_view, kM4GateCount> kM4GateNames = {
    "G1", "G2", "G3", "G4", "G5",
    "G6", "G7", "G8", "G9", "G10",
};

inline constexpr std::array<std::string_view, kM4GateCount>
    kM4GateDescriptions = {
        "10 boundary objects reviewed and frozen",
        "T022 object catalog complete (3/3/4 ADR waves)",
        "T022 forbidden-field catalog complete and guard dispatched",
        "ADR-006 preserves a single context owner",
        "ADR-007 preserves a single recovery owner",
        "ADR-008 preserves a single global owner",
        "Worker domain layering remains one-way",
        "Review conclusions are programmable as checklist gates",
        "Contract tests pass for boundary-freeze milestone",
        "T022 ADR mapping gate prerequisite passed",
    };

inline std::array<bool, kM4GateCount> m4_checklist_gate_values(
    const M4ChecklistInputs& inputs) {
  return {
      inputs.gate_g1_review_scope_complete,
      inputs.gate_g2_object_catalog_complete,
      inputs.gate_g3_forbidden_field_catalog_complete,
      inputs.gate_g4_adr006_single_context_owner,
      inputs.gate_g5_adr007_single_recovery_owner,
      inputs.gate_g6_adr008_single_global_owner,
      inputs.gate_g7_worker_domain_layering,
      inputs.gate_g8_checklist_programmable,
      inputs.gate_g9_contract_tests_passed,
      inputs.gate_g10_t022_mapping_passed,
  };
}

inline M4ChecklistResult validate_m4_checklist(const M4ChecklistInputs& inputs) {
  const auto gate_values = m4_checklist_gate_values(inputs);

  for (std::size_t index = 0; index < kM4GateCount; ++index) {
    if (!gate_values[index]) {
      return M4ChecklistResult{
          .ok = false,
          .gate_g1_passed = inputs.gate_g1_review_scope_complete,
          .gate_g2_passed = inputs.gate_g2_object_catalog_complete,
          .gate_g3_passed = inputs.gate_g3_forbidden_field_catalog_complete,
          .gate_g4_passed = inputs.gate_g4_adr006_single_context_owner,
          .gate_g5_passed = inputs.gate_g5_adr007_single_recovery_owner,
          .gate_g6_passed = inputs.gate_g6_adr008_single_global_owner,
          .gate_g7_passed = inputs.gate_g7_worker_domain_layering,
          .gate_g8_passed = inputs.gate_g8_checklist_programmable,
          .gate_g9_passed = inputs.gate_g9_contract_tests_passed,
          .gate_g10_passed = inputs.gate_g10_t022_mapping_passed,
          .first_failed_gate = kM4GateNames[index],
          .reason = "M4 checklist contains failed gate - WP04 freeze blocked",
      };
    }
  }

  return M4ChecklistResult{
      .ok = true,
      .gate_g1_passed = true,
      .gate_g2_passed = true,
      .gate_g3_passed = true,
      .gate_g4_passed = true,
      .gate_g5_passed = true,
      .gate_g6_passed = true,
      .gate_g7_passed = true,
      .gate_g8_passed = true,
      .gate_g9_passed = true,
      .gate_g10_passed = true,
      .first_failed_gate = "none",
      .reason = "M4 checklist passed - WP04 boundary freeze ready",
  };
}

inline std::size_t m4_count_passed_gates(const M4ChecklistInputs& inputs) {
  const auto values = m4_checklist_gate_values(inputs);
  std::size_t count = 0;
  for (bool value : values) {
    if (value) {
      ++count;
    }
  }
  return count;
}

}  // namespace dasall::contracts
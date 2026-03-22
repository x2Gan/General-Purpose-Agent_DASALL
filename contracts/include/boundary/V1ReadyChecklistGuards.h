#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

// Total number of V1 Ready gates frozen by WP05-T020.
inline constexpr std::size_t kV1ReadyGateCount = 10;

// V1ReadyChecklistInputs projects the WP05 review conclusions into executable
// gate booleans.
struct V1ReadyChecklistInputs {
  bool gate_g1_domain_rollout_passed = false;
  bool gate_g2_interface_admission_passed = false;
  bool gate_g3_serialization_compatibility_passed = false;
  bool gate_g4_error_enum_compatibility_passed = false;
  bool gate_g5_event_envelope_compatibility_passed = false;
  bool gate_g6_adr_boundary_regression_passed = false;
  bool gate_g7_coverage_matrix_passed = false;
  bool gate_g8_version_change_schema_passed = false;
  bool gate_g9_breaking_review_passed = false;
  bool gate_g10_wp05_contract_tests_passed = false;
};

// V1ReadyChecklistResult carries aggregate and per-gate state for CI and
// checklist reporting.
struct V1ReadyChecklistResult {
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
  std::string_view reason = "V1 ready checklist validation not yet run";
};

// Stable gate IDs used in diagnostics and tests.
inline constexpr std::array<std::string_view, kV1ReadyGateCount>
    kV1ReadyGateNames = {
        "G1", "G2", "G3", "G4", "G5", "G6", "G7", "G8", "G9", "G10",
};

// Human-readable gate descriptions aligned to the WP05-T020 review outcomes.
inline constexpr std::array<std::string_view, kV1ReadyGateCount>
    kV1ReadyGateDescriptions = {
        "Domain rollout guard baseline passed",
        "Interface catalog and admission guards passed",
        "Serialization compatibility contracts passed",
        "Error code and enum compatibility contracts passed",
        "EventEnvelope compatibility contracts passed",
        "ADR boundary regression contracts passed",
        "Coverage matrix guards passed",
        "Version change schema guards passed",
        "Breaking review gate guards passed",
        "WP05 full contract test suite passed",
};

// Converts checklist input fields to ordered gate booleans.
inline std::array<bool, kV1ReadyGateCount> v1_ready_gate_values(
    const V1ReadyChecklistInputs& inputs) {
  return {
      inputs.gate_g1_domain_rollout_passed,
      inputs.gate_g2_interface_admission_passed,
      inputs.gate_g3_serialization_compatibility_passed,
      inputs.gate_g4_error_enum_compatibility_passed,
      inputs.gate_g5_event_envelope_compatibility_passed,
      inputs.gate_g6_adr_boundary_regression_passed,
      inputs.gate_g7_coverage_matrix_passed,
      inputs.gate_g8_version_change_schema_passed,
      inputs.gate_g9_breaking_review_passed,
      inputs.gate_g10_wp05_contract_tests_passed,
  };
}

// Validates the full V1 Ready checklist and reports the first failing gate in
// order.
inline V1ReadyChecklistResult validate_v1_ready_checklist(
    const V1ReadyChecklistInputs& inputs) {
  const auto gate_values = v1_ready_gate_values(inputs);

  for (std::size_t index = 0; index < kV1ReadyGateCount; ++index) {
    if (!gate_values[index]) {
      return V1ReadyChecklistResult{
          .ok = false,
          .gate_g1_passed = inputs.gate_g1_domain_rollout_passed,
          .gate_g2_passed = inputs.gate_g2_interface_admission_passed,
          .gate_g3_passed = inputs.gate_g3_serialization_compatibility_passed,
          .gate_g4_passed = inputs.gate_g4_error_enum_compatibility_passed,
          .gate_g5_passed = inputs.gate_g5_event_envelope_compatibility_passed,
          .gate_g6_passed = inputs.gate_g6_adr_boundary_regression_passed,
          .gate_g7_passed = inputs.gate_g7_coverage_matrix_passed,
          .gate_g8_passed = inputs.gate_g8_version_change_schema_passed,
          .gate_g9_passed = inputs.gate_g9_breaking_review_passed,
          .gate_g10_passed = inputs.gate_g10_wp05_contract_tests_passed,
          .first_failed_gate = kV1ReadyGateNames[index],
          .reason = "V1 ready checklist contains failed gate - WP05 freeze blocked",
      };
    }
  }

  return V1ReadyChecklistResult{
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
      .reason = "V1 ready checklist passed - WP05 freeze ready",
  };
}

// Counts how many gates are currently passing.
inline std::size_t v1_ready_count_passed_gates(
    const V1ReadyChecklistInputs& inputs) {
  const auto values = v1_ready_gate_values(inputs);
  std::size_t count = 0;
  for (bool value : values) {
    if (value) {
      ++count;
    }
  }
  return count;
}

}  // namespace dasall::contracts
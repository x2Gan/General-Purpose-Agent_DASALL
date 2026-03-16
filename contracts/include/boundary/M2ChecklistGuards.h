#pragma once

#include <array>
#include <string_view>

namespace dasall::contracts {

// M2ChecklistInputs is the executable projection of WP02-T013 checklist
// groups A-F. Each boolean represents whether one gate group has passed.
struct M2ChecklistInputs {
  bool gate_a_passed = false;
  bool gate_b_passed = false;
  bool gate_c_passed = false;
  bool gate_d_passed = false;
  bool gate_e_passed = false;
  bool gate_f_passed = false;
};

struct M2ChecklistResult {
  bool ok = false;
  bool gate_a_passed = false;
  bool gate_b_passed = false;
  bool gate_c_passed = false;
  bool gate_d_passed = false;
  bool gate_e_passed = false;
  bool gate_f_passed = false;
  std::string_view first_failed_gate = "none";
  std::string_view reason = "M2 checklist validation failed";
};

inline std::array<bool, 6> checklist_gate_values(const M2ChecklistInputs& inputs) {
  return std::array<bool, 6>{
      inputs.gate_a_passed,
      inputs.gate_b_passed,
      inputs.gate_c_passed,
      inputs.gate_d_passed,
      inputs.gate_e_passed,
      inputs.gate_f_passed,
  };
}

inline std::array<std::string_view, 6> checklist_gate_names() {
  return std::array<std::string_view, 6>{"A", "B", "C", "D", "E", "F"};
}

// Validates M2 gate readiness by requiring all checklist groups A-F to pass.
// The result carries per-group states and the first failed gate for audit.
inline M2ChecklistResult validate_m2_checklist(const M2ChecklistInputs& inputs) {
  const auto gate_values = checklist_gate_values(inputs);
  const auto gate_names = checklist_gate_names();

  for (std::size_t i = 0; i < gate_values.size(); ++i) {
    if (!gate_values[i]) {
      return M2ChecklistResult{
          .ok = false,
          .gate_a_passed = inputs.gate_a_passed,
          .gate_b_passed = inputs.gate_b_passed,
          .gate_c_passed = inputs.gate_c_passed,
          .gate_d_passed = inputs.gate_d_passed,
          .gate_e_passed = inputs.gate_e_passed,
          .gate_f_passed = inputs.gate_f_passed,
          .first_failed_gate = gate_names[i],
          .reason = "M2 checklist contains failed gate group",
      };
    }
  }

  return M2ChecklistResult{
      .ok = true,
      .gate_a_passed = true,
      .gate_b_passed = true,
      .gate_c_passed = true,
      .gate_d_passed = true,
      .gate_e_passed = true,
      .gate_f_passed = true,
      .first_failed_gate = "none",
      .reason = "M2 checklist is valid",
  };
}

}  // namespace dasall::contracts

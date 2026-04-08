#include <exception>
#include <iostream>
#include <string>

#include "boundary/M2ChecklistGuards.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::M2ChecklistInputs make_all_gates_passed_inputs() {
  return dasall::contracts::M2ChecklistInputs{
      .gate_a_passed = true,
      .gate_b_passed = true,
      .gate_c_passed = true,
      .gate_d_passed = true,
      .gate_e_passed = true,
      .gate_f_passed = true,
  };
}

void test_all_gate_groups_pass_checklist() {
  using dasall::contracts::validate_m2_checklist;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Positive case: all A-F gate groups are passed, so checklist is valid.
  const auto inputs = make_all_gates_passed_inputs();
  const auto result = validate_m2_checklist(inputs);

  assert_true(result.ok, "all-passed checklist should be valid");
  assert_equal("none",
               std::string(result.first_failed_gate),
               "valid checklist should have no failed gate marker");
}

void test_failed_gate_group_blocks_checklist() {
  using dasall::contracts::validate_m2_checklist;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  // Negative case: if one gate group fails, the checklist must fail and report
  // the first failed gate for deterministic debugging.
  auto inputs = make_all_gates_passed_inputs();
  inputs.gate_c_passed = false;

  const auto result = validate_m2_checklist(inputs);

  assert_true(!result.ok, "failed gate group should block M2 checklist");
  assert_equal("C",
               std::string(result.first_failed_gate),
               "checklist should report first failed gate group");
}

}  // namespace

int main() {
  try {
    test_all_gate_groups_pass_checklist();
    test_failed_gate_group_blocks_checklist();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

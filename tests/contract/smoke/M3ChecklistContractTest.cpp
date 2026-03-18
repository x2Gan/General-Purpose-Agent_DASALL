// ==========================================================================
// M3ChecklistContractTest.cpp
//
// WP03-T017-B: Contract smoke test for M3ChecklistGuards.h.
// Validates that:
//   - All 10 gates passing yields a valid M3 checklist (positive cases)
//   - A single failed gate blocks the checklist and reports correctly
//     (negative cases)
//   - Gate metadata arrays (names, descriptions) are consistent
//   - m3_count_passed_gates helper works correctly
//
// Test structure:
//   Positive (4):
//     1. All 10 gates pass → checklist ok, first_failed_gate == "none"
//     2. Gate name array has exactly kM3GateCount entries
//     3. Gate description array has exactly kM3GateCount entries
//     4. m3_count_passed_gates returns 10 for all-pass input
//   Negative (6):
//     1. G1 failed → blocks with first_failed_gate == "G1"
//     2. G4 (ADR-006) failed → blocks with first_failed_gate == "G4"
//     3. G7 (E2E) failed → blocks with first_failed_gate == "G7"
//     4. G10 (M2 prerequisite) failed → blocks with "G10"
//     5. Multiple gates failed → reports first (lowest numbered) gate
//     6. m3_count_passed_gates returns correct count for partial input
//
// Verification command:
//   cmake --build build-ci --target dasall_contract_tests && \
//   ctest --test-dir build-ci -R M3ChecklistContractTest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>

#include "boundary/M3ChecklistGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::M3ChecklistInputs;
using dasall::contracts::M3ChecklistResult;
using dasall::contracts::kM3GateCount;
using dasall::contracts::kM3GateDescriptions;
using dasall::contracts::kM3GateNames;
using dasall::contracts::m3_count_passed_gates;
using dasall::contracts::validate_m3_checklist;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

/// Helper: create an M3ChecklistInputs with all gates passed.
M3ChecklistInputs make_all_gates_passed() {
  return M3ChecklistInputs{
      .gate_g1_required_field_guards  = true,
      .gate_g2_forbidden_domain_cover = true,
      .gate_g3_symmetric_exclusion    = true,
      .gate_g4_adr006_context_packet  = true,
      .gate_g5_adr007_checkpoint      = true,
      .gate_g6_adr008_agent_result    = true,
      .gate_g7_e2e_correlation        = true,
      .gate_g8_domain_uniqueness      = true,
      .gate_g9_contract_tests_passed  = true,
      .gate_g10_m2_checklist_passed   = true,
  };
}

// =========================================================================
// Positive case 1: All 10 gates pass → checklist ok
// =========================================================================
void test_all_gates_pass_m3_checklist() {
  const auto inputs = make_all_gates_passed();
  const auto result = validate_m3_checklist(inputs);

  assert_true(result.ok,
              "all-pass M3 checklist must be valid");
  assert_equal(std::string("none"),
               std::string(result.first_failed_gate),
               "valid checklist should have no failed gate");

  // Verify all per-gate flags are true.
  assert_true(result.gate_g1_passed,  "G1 should pass");
  assert_true(result.gate_g2_passed,  "G2 should pass");
  assert_true(result.gate_g3_passed,  "G3 should pass");
  assert_true(result.gate_g4_passed,  "G4 should pass");
  assert_true(result.gate_g5_passed,  "G5 should pass");
  assert_true(result.gate_g6_passed,  "G6 should pass");
  assert_true(result.gate_g7_passed,  "G7 should pass");
  assert_true(result.gate_g8_passed,  "G8 should pass");
  assert_true(result.gate_g9_passed,  "G9 should pass");
  assert_true(result.gate_g10_passed, "G10 should pass");
}

// =========================================================================
// Positive case 2: Gate name array size consistency
// =========================================================================
void test_gate_name_array_size() {
  assert_equal(static_cast<int>(kM3GateCount),
               static_cast<int>(kM3GateNames.size()),
               "kM3GateNames must have kM3GateCount entries");
  // Verify first and last entries are meaningful.
  assert_equal(std::string("G1"),
               std::string(kM3GateNames[0]),
               "first gate name should be G1");
  assert_equal(std::string("G10"),
               std::string(kM3GateNames[kM3GateCount - 1]),
               "last gate name should be G10");
}

// =========================================================================
// Positive case 3: Gate description array size consistency
// =========================================================================
void test_gate_description_array_size() {
  assert_equal(static_cast<int>(kM3GateCount),
               static_cast<int>(kM3GateDescriptions.size()),
               "kM3GateDescriptions must have kM3GateCount entries");
  // Ensure descriptions are non-empty.
  for (std::size_t i = 0; i < kM3GateCount; ++i) {
    assert_true(!kM3GateDescriptions[i].empty(),
                "gate description must not be empty");
  }
}

// =========================================================================
// Positive case 4: m3_count_passed_gates returns 10 for all-pass
// =========================================================================
void test_count_passed_gates_all_pass() {
  const auto inputs = make_all_gates_passed();
  assert_equal(static_cast<int>(kM3GateCount),
               static_cast<int>(m3_count_passed_gates(inputs)),
               "all-pass input should yield count == kM3GateCount");
}

// =========================================================================
// Negative case 1: G1 failed → blocks checklist
// =========================================================================
void test_g1_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g1_required_field_guards = false;

  const auto result = validate_m3_checklist(inputs);
  assert_true(!result.ok,
              "G1 failure must block M3 checklist");
  assert_equal(std::string("G1"),
               std::string(result.first_failed_gate),
               "first_failed_gate should be G1");
  assert_true(!result.gate_g1_passed,
              "per-gate G1 should report false");
}

// =========================================================================
// Negative case 2: G4 (ADR-006) failed → blocks checklist
// =========================================================================
void test_g4_adr006_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g4_adr006_context_packet = false;

  const auto result = validate_m3_checklist(inputs);
  assert_true(!result.ok,
              "G4 failure must block M3 checklist");
  assert_equal(std::string("G4"),
               std::string(result.first_failed_gate),
               "first_failed_gate should be G4");
  assert_true(!result.gate_g4_passed,
              "per-gate G4 should report false");
  // Gates before G4 should still show as passed.
  assert_true(result.gate_g1_passed, "G1 before G4 should still pass");
  assert_true(result.gate_g2_passed, "G2 before G4 should still pass");
  assert_true(result.gate_g3_passed, "G3 before G4 should still pass");
}

// =========================================================================
// Negative case 3: G7 (E2E correlation) failed → blocks checklist
// =========================================================================
void test_g7_e2e_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g7_e2e_correlation = false;

  const auto result = validate_m3_checklist(inputs);
  assert_true(!result.ok,
              "G7 failure must block M3 checklist");
  assert_equal(std::string("G7"),
               std::string(result.first_failed_gate),
               "first_failed_gate should be G7");
}

// =========================================================================
// Negative case 4: G10 (M2 prerequisite) failed → blocks checklist
// =========================================================================
void test_g10_m2_prerequisite_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g10_m2_checklist_passed = false;

  const auto result = validate_m3_checklist(inputs);
  assert_true(!result.ok,
              "G10 failure must block M3 checklist");
  assert_equal(std::string("G10"),
               std::string(result.first_failed_gate),
               "first_failed_gate should be G10");
  assert_true(!result.gate_g10_passed,
              "per-gate G10 should report false");
  // All gates before G10 should still show as passed.
  assert_true(result.gate_g9_passed,
              "G9 before G10 should still pass");
}

// =========================================================================
// Negative case 5: Multiple gates failed → reports first (lowest)
// =========================================================================
void test_multiple_failures_report_first() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g3_symmetric_exclusion  = false;  // G3
  inputs.gate_g6_adr008_agent_result  = false;  // G6
  inputs.gate_g9_contract_tests_passed = false;  // G9

  const auto result = validate_m3_checklist(inputs);
  assert_true(!result.ok,
              "multiple failures must block checklist");
  // G3 is lowest numbered among the failed gates → reported first.
  assert_equal(std::string("G3"),
               std::string(result.first_failed_gate),
               "should report first (lowest) failed gate G3");
}

// =========================================================================
// Negative case 6: m3_count_passed_gates for partial input
// =========================================================================
void test_count_passed_gates_partial() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g2_forbidden_domain_cover = false;  // G2 off
  inputs.gate_g5_adr007_checkpoint      = false;  // G5 off
  inputs.gate_g8_domain_uniqueness      = false;  // G8 off

  // 10 - 3 = 7 gates should pass.
  assert_equal(7,
               static_cast<int>(m3_count_passed_gates(inputs)),
               "3 gates failed → 7 should pass");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " — " << ex.what() << "\n";
    }
  };

  std::cout << "M3ChecklistContractTest — WP03-T017-B\n";

  // Positive cases
  run_test("test_all_gates_pass_m3_checklist",
           test_all_gates_pass_m3_checklist);
  run_test("test_gate_name_array_size",
           test_gate_name_array_size);
  run_test("test_gate_description_array_size",
           test_gate_description_array_size);
  run_test("test_count_passed_gates_all_pass",
           test_count_passed_gates_all_pass);

  // Negative cases
  run_test("test_g1_failed_blocks_checklist",
           test_g1_failed_blocks_checklist);
  run_test("test_g4_adr006_failed_blocks_checklist",
           test_g4_adr006_failed_blocks_checklist);
  run_test("test_g7_e2e_failed_blocks_checklist",
           test_g7_e2e_failed_blocks_checklist);
  run_test("test_g10_m2_prerequisite_failed_blocks_checklist",
           test_g10_m2_prerequisite_failed_blocks_checklist);
  run_test("test_multiple_failures_report_first",
           test_multiple_failures_report_first);
  run_test("test_count_passed_gates_partial",
           test_count_passed_gates_partial);

  std::cout << "\nResults: " << passed << " passed, " << failed << " failed, "
            << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}

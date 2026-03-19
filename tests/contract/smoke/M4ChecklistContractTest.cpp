// ============================================================================
// M4ChecklistContractTest.cpp
//
// WP04-T023-B: Smoke contract test for M4ChecklistGuards.h.
// Verifies that the review conclusions from WP04-T023-D remain consumable as a
// deterministic checklist gate.
//
// Positive coverage:
//   - all 10 gates passing yields a valid checklist
//   - gate metadata arrays stay aligned with kM4GateCount
//   - passed-gate counting works for the all-pass path
//
// Negative coverage:
//   - a failed single-owner boundary gate blocks the checklist
//   - a failed contract-test gate blocks the checklist
//   - multiple failures report the first failed gate in order
//   - partial inputs count only the passed gates
// ============================================================================
#include <exception>
#include <iostream>
#include <string>

#include "boundary/M4ChecklistGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::M4ChecklistInputs;
using dasall::contracts::kM4GateCount;
using dasall::contracts::kM4GateDescriptions;
using dasall::contracts::kM4GateNames;
using dasall::contracts::m4_count_passed_gates;
using dasall::contracts::validate_m4_checklist;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

M4ChecklistInputs make_all_gates_passed() {
  return M4ChecklistInputs{
      .gate_g1_review_scope_complete = true,
      .gate_g2_object_catalog_complete = true,
      .gate_g3_forbidden_field_catalog_complete = true,
      .gate_g4_adr006_single_context_owner = true,
      .gate_g5_adr007_single_recovery_owner = true,
      .gate_g6_adr008_single_global_owner = true,
      .gate_g7_worker_domain_layering = true,
      .gate_g8_checklist_programmable = true,
      .gate_g9_contract_tests_passed = true,
      .gate_g10_t022_mapping_passed = true,
  };
}

void test_all_gates_pass_m4_checklist() {
  const auto inputs = make_all_gates_passed();
  const auto result = validate_m4_checklist(inputs);

  assert_true(result.ok, "all-pass M4 checklist must be valid");
  assert_equal(std::string("none"),
               std::string(result.first_failed_gate),
               "valid M4 checklist should report no failed gate");
  assert_true(result.gate_g4_passed,
              "ADR-006 single-owner gate must pass in the happy path");
  assert_true(result.gate_g6_passed,
              "ADR-008 single-owner gate must pass in the happy path");
  assert_true(result.gate_g10_passed,
              "T022 prerequisite gate must pass in the happy path");
}

void test_gate_name_array_size() {
  assert_equal(static_cast<int>(kM4GateCount),
               static_cast<int>(kM4GateNames.size()),
               "kM4GateNames must expose kM4GateCount entries");
  assert_equal(std::string("G1"),
               std::string(kM4GateNames.front()),
               "first M4 gate name must be G1");
  assert_equal(std::string("G10"),
               std::string(kM4GateNames.back()),
               "last M4 gate name must be G10");
}

void test_gate_description_array_size() {
  assert_equal(static_cast<int>(kM4GateCount),
               static_cast<int>(kM4GateDescriptions.size()),
               "kM4GateDescriptions must expose kM4GateCount entries");
  for (std::size_t index = 0; index < kM4GateCount; ++index) {
    assert_true(!kM4GateDescriptions[index].empty(),
                "M4 gate description must not be empty");
  }
}

void test_count_passed_gates_all_pass() {
  const auto inputs = make_all_gates_passed();
  assert_equal(static_cast<int>(kM4GateCount),
               static_cast<int>(m4_count_passed_gates(inputs)),
               "all-pass M4 input should yield kM4GateCount passed gates");
}

void test_g4_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g4_adr006_single_context_owner = false;

  const auto result = validate_m4_checklist(inputs);
  assert_true(!result.ok, "G4 failure must block M4 checklist");
  assert_equal(std::string("G4"),
               std::string(result.first_failed_gate),
               "first failed gate should be G4 when ADR-006 regresses");
  assert_true(!result.gate_g4_passed,
              "per-gate G4 status must report false on regression");
}

void test_g6_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g6_adr008_single_global_owner = false;

  const auto result = validate_m4_checklist(inputs);
  assert_true(!result.ok, "G6 failure must block M4 checklist");
  assert_equal(std::string("G6"),
               std::string(result.first_failed_gate),
               "first failed gate should be G6 when ADR-008 regresses");
}

void test_g9_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g9_contract_tests_passed = false;

  const auto result = validate_m4_checklist(inputs);
  assert_true(!result.ok, "G9 failure must block M4 checklist");
  assert_equal(std::string("G9"),
               std::string(result.first_failed_gate),
               "first failed gate should be G9 when contract tests regress");
  assert_true(result.gate_g8_passed,
              "gates before G9 should still report pass");
}

void test_g10_failed_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g10_t022_mapping_passed = false;

  const auto result = validate_m4_checklist(inputs);
  assert_true(!result.ok, "G10 failure must block M4 checklist");
  assert_equal(std::string("G10"),
               std::string(result.first_failed_gate),
               "first failed gate should be G10 when T022 prerequisite regresses");
}

void test_multiple_failures_report_first() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g3_forbidden_field_catalog_complete = false;
  inputs.gate_g7_worker_domain_layering = false;
  inputs.gate_g10_t022_mapping_passed = false;

  const auto result = validate_m4_checklist(inputs);
  assert_true(!result.ok, "multiple failures must block M4 checklist");
  assert_equal(std::string("G3"),
               std::string(result.first_failed_gate),
               "the first failed gate must be the lowest-numbered failed gate");
}

void test_count_passed_gates_partial() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g2_object_catalog_complete = false;
  inputs.gate_g5_adr007_single_recovery_owner = false;
  inputs.gate_g8_checklist_programmable = false;

  assert_equal(7,
               static_cast<int>(m4_count_passed_gates(inputs)),
               "three failed M4 gates should leave seven passing gates");
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
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  std::cout << "M4ChecklistContractTest - WP04-T023-B\n";

  run_test("test_all_gates_pass_m4_checklist",
           test_all_gates_pass_m4_checklist);
  run_test("test_gate_name_array_size", test_gate_name_array_size);
  run_test("test_gate_description_array_size",
           test_gate_description_array_size);
  run_test("test_count_passed_gates_all_pass",
           test_count_passed_gates_all_pass);
  run_test("test_g4_failed_blocks_checklist",
           test_g4_failed_blocks_checklist);
  run_test("test_g6_failed_blocks_checklist",
           test_g6_failed_blocks_checklist);
  run_test("test_g9_failed_blocks_checklist",
           test_g9_failed_blocks_checklist);
  run_test("test_g10_failed_blocks_checklist",
           test_g10_failed_blocks_checklist);
  run_test("test_multiple_failures_report_first",
           test_multiple_failures_report_first);
  run_test("test_count_passed_gates_partial",
           test_count_passed_gates_partial);

  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
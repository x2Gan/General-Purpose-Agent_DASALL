#include <exception>
#include <iostream>
#include <string>

#include "boundary/V1ReadyChecklistGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::V1ReadyChecklistInputs;
using dasall::contracts::kV1ReadyGateCount;
using dasall::contracts::kV1ReadyGateDescriptions;
using dasall::contracts::kV1ReadyGateNames;
using dasall::contracts::v1_ready_count_passed_gates;
using dasall::contracts::validate_v1_ready_checklist;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds the all-pass input used by the success-path and partial-count tests.
V1ReadyChecklistInputs make_all_gates_passed() {
  return V1ReadyChecklistInputs{
      .gate_g1_domain_rollout_passed = true,
      .gate_g2_interface_admission_passed = true,
      .gate_g3_serialization_compatibility_passed = true,
      .gate_g4_error_enum_compatibility_passed = true,
      .gate_g5_event_envelope_compatibility_passed = true,
      .gate_g6_adr_boundary_regression_passed = true,
      .gate_g7_coverage_matrix_passed = true,
      .gate_g8_version_change_schema_passed = true,
      .gate_g9_breaking_review_passed = true,
      .gate_g10_wp05_contract_tests_passed = true,
  };
}

// Positive coverage: all gates passing should produce a valid V1 ready result.
void test_all_gates_pass_v1_ready_checklist() {
  const auto inputs = make_all_gates_passed();
  const auto result = validate_v1_ready_checklist(inputs);

  assert_true(result.ok, "all-pass V1 checklist must be valid");
  assert_equal(std::string("none"),
               std::string(result.first_failed_gate),
               "valid checklist should not report a failed gate");
}

// Positive coverage: gate metadata arrays must stay aligned with the declared
// gate count constant.
void test_gate_metadata_alignment() {
  assert_equal(static_cast<int>(kV1ReadyGateCount),
               static_cast<int>(kV1ReadyGateNames.size()),
               "gate-name list must match gate count");
  assert_equal(static_cast<int>(kV1ReadyGateCount),
               static_cast<int>(kV1ReadyGateDescriptions.size()),
               "gate-description list must match gate count");
}

// Negative coverage: a rollout baseline failure should block the checklist at
// G1.
void test_g1_failure_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g1_domain_rollout_passed = false;

  const auto result = validate_v1_ready_checklist(inputs);
  assert_true(!result.ok, "G1 failure must block V1 checklist");
  assert_equal(std::string("G1"),
               std::string(result.first_failed_gate),
               "first failed gate should be G1");
}

// Negative coverage: breaking-review failure should block the checklist at G9
// when all previous gates pass.
void test_g9_failure_blocks_checklist() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g9_breaking_review_passed = false;

  const auto result = validate_v1_ready_checklist(inputs);
  assert_true(!result.ok, "G9 failure must block V1 checklist");
  assert_equal(std::string("G9"),
               std::string(result.first_failed_gate),
               "first failed gate should be G9");
}

// Negative coverage: if multiple gates fail, the result must report the first
// failure by gate order.
void test_multiple_failures_report_first_gate() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g4_error_enum_compatibility_passed = false;
  inputs.gate_g7_coverage_matrix_passed = false;
  inputs.gate_g10_wp05_contract_tests_passed = false;

  const auto result = validate_v1_ready_checklist(inputs);
  assert_true(!result.ok, "multiple failures must block checklist");
  assert_equal(std::string("G4"),
               std::string(result.first_failed_gate),
               "first failed gate should be the lowest-index failed gate");
}

// Positive coverage: passed-gate count helper must return exact count.
void test_count_passed_gates_partial() {
  auto inputs = make_all_gates_passed();
  inputs.gate_g3_serialization_compatibility_passed = false;
  inputs.gate_g8_version_change_schema_passed = false;

  assert_equal(8,
               static_cast<int>(v1_ready_count_passed_gates(inputs)),
               "two failing gates should leave eight passed gates");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Common runner keeps this smoke test aligned with repository output style.
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

  // Banner keeps ctest output traceable to WP05-T020-B.
  std::cout << "V1ReadyChecklistContractTest - WP05-T020-B\n";

  run_test("test_all_gates_pass_v1_ready_checklist",
           test_all_gates_pass_v1_ready_checklist);
  run_test("test_gate_metadata_alignment",
           test_gate_metadata_alignment);
  run_test("test_g1_failure_blocks_checklist",
           test_g1_failure_blocks_checklist);
  run_test("test_g9_failure_blocks_checklist",
           test_g9_failure_blocks_checklist);
  run_test("test_multiple_failures_report_first_gate",
           test_multiple_failures_report_first_gate);
  run_test("test_count_passed_gates_partial",
           test_count_passed_gates_partial);

  // Summary output follows the same contract-test convention as other smoke
  // executables.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
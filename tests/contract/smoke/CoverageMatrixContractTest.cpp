#include <exception>
#include <iostream>
#include <string>

#include "boundary/CoverageMatrixGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::CoverageMatrixExecutionSnapshot;
using dasall::contracts::count_catalog_mappings_for_risk_object;
using dasall::contracts::count_uncovered_risk_objects;
using dasall::contracts::first_uncovered_risk_object_name;
using dasall::contracts::has_minimum_contract_test_for_every_risk_object;
using dasall::contracts::is_risk_object_covered;
using dasall::contracts::kCoverageRiskObjectCount;
using dasall::contracts::validate_coverage_matrix;
using dasall::contracts::CoverageRiskObject;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds a full-pass snapshot that represents the expected stable baseline
// after WP05-T013-B through WP05-T016-B tests are all executed.
CoverageMatrixExecutionSnapshot make_full_pass_snapshot() {
  CoverageMatrixExecutionSnapshot snapshot;
  snapshot.serialization_compatibility_passed = true;
  snapshot.error_code_enum_compatibility_passed = true;
  snapshot.event_envelope_compatibility_passed = true;
  snapshot.adr_boundary_regression_passed = true;
  return snapshot;
}

// Positive coverage: every high-risk object must have at least one mapping in
// the coverage catalog so gaps are impossible to hide.
void test_catalog_has_mapping_for_every_high_risk_object() {
  assert_true(has_minimum_contract_test_for_every_risk_object(),
              "every high-risk object must map to at least one contract test");
  assert_equal(1,
               static_cast<int>(count_catalog_mappings_for_risk_object(
                   CoverageRiskObject::AgentRequest)),
               "AgentRequest should keep one stable mapping");
  assert_equal(2,
               static_cast<int>(count_catalog_mappings_for_risk_object(
                   CoverageRiskObject::EventEnvelope)),
               "EventEnvelope should keep two stable mappings");
}

// Positive coverage: full-pass snapshot should pass the coverage matrix guard
// with no uncovered object.
void test_full_pass_snapshot_validates_successfully() {
  const auto result = validate_coverage_matrix(make_full_pass_snapshot());

  assert_true(result.ok,
              "full-pass snapshot should satisfy coverage matrix guard");
  assert_true(result.catalog_complete,
              "catalog should be marked complete on success");
  assert_true(result.all_risk_objects_covered,
              "all high-risk objects should be covered on success");
  assert_equal(0,
               static_cast<int>(result.uncovered_risk_object_count),
               "uncovered object count should be zero on success");
}

// Negative coverage: if serialization compatibility is not passed, the first
// uncovered object should deterministically point to AgentRequest.
void test_missing_serialization_detects_agent_request_gap() {
  CoverageMatrixExecutionSnapshot snapshot = make_full_pass_snapshot();
  snapshot.serialization_compatibility_passed = false;

  const auto result = validate_coverage_matrix(snapshot);

  assert_true(!result.ok,
              "missing serialization compatibility must fail coverage matrix");
  assert_equal(std::string("AgentRequest"),
               std::string(result.first_uncovered_risk_object),
               "first uncovered object should be AgentRequest");
  assert_equal(std::string("risk-object-coverage"),
               std::string(result.first_failed_check),
               "failure check name should report risk-object coverage");
}

// Negative coverage: if ADR boundary regression is not passed, ADR objects
// should be reported as uncovered so boundary drift can be blocked.
void test_missing_adr_regression_detects_context_packet_gap() {
  CoverageMatrixExecutionSnapshot snapshot = make_full_pass_snapshot();
  snapshot.adr_boundary_regression_passed = false;

  assert_true(!is_risk_object_covered(snapshot, CoverageRiskObject::ContextPacket),
              "ContextPacket must be uncovered when ADR regression is missing");
  assert_equal(std::string("ContextPacket"),
               std::string(first_uncovered_risk_object_name(snapshot)),
               "first uncovered object should be ContextPacket");
  assert_equal(3,
               static_cast<int>(count_uncovered_risk_objects(snapshot)),
               "three ADR-boundary objects should become uncovered");
}

// Negative coverage: if all matrix tests are missing, uncovered count should
// equal the full high-risk object count.
void test_empty_snapshot_marks_all_objects_uncovered() {
  CoverageMatrixExecutionSnapshot snapshot;

  assert_equal(static_cast<int>(kCoverageRiskObjectCount),
               static_cast<int>(count_uncovered_risk_objects(snapshot)),
               "empty snapshot should uncover every high-risk object");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Common runner keeps this smoke executable aligned with existing contract
  // test output format for quick CI log scanning.
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

  // Banner text maps ctest output directly to WP05-T017-B.
  std::cout << "CoverageMatrixContractTest - WP05-T017-B\n";

  run_test("test_catalog_has_mapping_for_every_high_risk_object",
           test_catalog_has_mapping_for_every_high_risk_object);
  run_test("test_full_pass_snapshot_validates_successfully",
           test_full_pass_snapshot_validates_successfully);
  run_test("test_missing_serialization_detects_agent_request_gap",
           test_missing_serialization_detects_agent_request_gap);
  run_test("test_missing_adr_regression_detects_context_packet_gap",
           test_missing_adr_regression_detects_context_packet_gap);
  run_test("test_empty_snapshot_marks_all_objects_uncovered",
           test_empty_snapshot_marks_all_objects_uncovered);

  // Summary output follows repository convention used by other smoke tests.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
#include <exception>
#include <iostream>
#include <string>

#include "boundary/InterfaceAdmissionGuards.h"
#include "boundary/InterfaceCatalog.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::InterfaceAdmissionDecision;
using dasall::contracts::InterfaceAdmissionReadiness;
using dasall::contracts::InterfaceCandidate;
using dasall::contracts::InterfaceCatalogEntry;
using dasall::contracts::InterfaceOwnerModule;
using dasall::contracts::InterfacePrimaryConsumer;
using dasall::contracts::can_admit_interface_candidate;
using dasall::contracts::count_admitted_interface_candidates;
using dasall::contracts::evaluate_interface_admission;
using dasall::contracts::evaluate_interface_admission_by_name;
using dasall::contracts::evaluate_interface_admission_entry;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Positive coverage: tool governance already has a closed contract-object set,
// so it should be admitted into the shared contract interface baseline.
void test_tool_manager_is_admitted() {
  const auto result =
      evaluate_interface_admission(InterfaceCandidate::IToolManager);

  assert_true(result.admitted,
              "IToolManager must be admitted into shared contracts");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Admit),
               static_cast<int>(result.decision),
               "IToolManager must return the admit decision");
  assert_equal(std::string("interface candidate is admitted into shared contracts"),
               std::string(result.reason),
               "IToolManager must report the stable admit reason");
}

// Positive coverage: the admission baseline now contains four interfaces after
// CAP-TODO-033, and the llm adapter remains one of the admitted candidates.
void test_llm_adapter_is_admitted_and_baseline_count_is_four() {
  const auto result =
      evaluate_interface_admission_by_name("ILLMAdapter");

  assert_true(result.admitted,
              "ILLMAdapter must be admitted into shared contracts");
  assert_true(can_admit_interface_candidate(InterfaceCandidate::ILLMAdapter),
              "can_admit helper must agree for ILLMAdapter");
  assert_equal(4,
               static_cast<int>(count_admitted_interface_candidates()),
               "current admission baseline must contain four interfaces after CAP-TODO-033");
}

// Positive coverage: services now has frozen supporting request/result objects
// plus integration evidence, so both service facades should be admitted.
void test_services_interfaces_are_admitted() {
  const auto execution_result =
      evaluate_interface_admission_by_name("IExecutionService");
  assert_true(execution_result.admitted,
              "IExecutionService must be admitted after CAP-TODO-033");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Admit),
               static_cast<int>(execution_result.decision),
               "IExecutionService must return the admit decision");
  assert_equal(std::string("interface candidate is admitted into shared contracts"),
               std::string(execution_result.reason),
               "IExecutionService must report the stable admit reason");

  const auto data_result =
      evaluate_interface_admission(InterfaceCandidate::IDataService);
  assert_true(data_result.admitted,
              "IDataService must be admitted after CAP-TODO-033");
  assert_true(can_admit_interface_candidate(InterfaceCandidate::IDataService),
              "can_admit helper must agree for IDataService");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Admit),
               static_cast<int>(data_result.decision),
               "IDataService must return the admit decision");
}

// Negative coverage: candidates that still depend on unfrozen supporting
// contracts must be postponed rather than prematurely admitted or returned.
void test_planner_is_postponed_until_supporting_contracts_freeze() {
  const auto result = evaluate_interface_admission(InterfaceCandidate::IPlanner);

  assert_true(!result.admitted,
              "IPlanner must not be admitted before PlanGraph freezes");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Postpone),
               static_cast<int>(result.decision),
               "IPlanner must return the postpone decision");
  assert_equal(
      std::string("interface candidate is postponed until supporting contracts freeze"),
      std::string(result.reason),
      "IPlanner must report the stable postpone reason");
}

// Negative coverage: directory misses must return to module scope, because T012
// only evaluates interfaces that already survived T011 candidate screening.
void test_non_catalogued_interface_is_returned() {
  const auto result = evaluate_interface_admission_by_name("IMCPAdapter");

  assert_true(!result.admitted,
              "IMCPAdapter must not be admitted because it is outside the catalog");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Return),
               static_cast<int>(result.decision),
               "non-catalogued interfaces must return to module scope");
  assert_equal(
      std::string("interface candidate must return to module scope because it is not catalogued"),
      std::string(result.reason),
      "non-catalogued interfaces must report the stable return reason");
}

// Negative coverage: metadata completeness is required for review traceability,
// so incomplete rows are returned even if their readiness says review-ready.
void test_incomplete_metadata_is_returned() {
  InterfaceCatalogEntry incomplete_entry{
      .candidate = InterfaceCandidate::IPlanner,
      .name = "IPlanner",
      .owner_module = InterfaceOwnerModule::Cognition,
      .primary_consumer = InterfacePrimaryConsumer::Runtime,
      .readiness = InterfaceAdmissionReadiness::ReviewReady,
      .stable_anchor = "",
      .rationale = "",
  };

  const auto result = evaluate_interface_admission_entry(incomplete_entry);

  assert_true(!result.admitted,
              "incomplete catalog metadata must block admission");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Return),
               static_cast<int>(result.decision),
               "incomplete metadata must return to module scope");
  assert_equal(
      std::string("interface candidate must return to module scope because catalog metadata is incomplete"),
      std::string(result.reason),
      "incomplete metadata must report the stable return reason");
}

// Negative coverage: same-module dependencies are not shared contract
// boundaries, so they must return even if the row otherwise looks complete.
void test_same_module_dependency_is_returned() {
  InterfaceCatalogEntry internal_entry{
      .candidate = InterfaceCandidate::IToolManager,
      .name = "IToolManager",
      .owner_module = InterfaceOwnerModule::Tools,
      .primary_consumer = InterfacePrimaryConsumer::Tools,
      .readiness = InterfaceAdmissionReadiness::ReviewReady,
      .stable_anchor = "ToolRequest/ToolResult/ToolDescriptor",
      .rationale = "synthetic same-module dependency for negative coverage",
  };

  const auto result = evaluate_interface_admission_entry(internal_entry);

  assert_true(!result.admitted,
              "same-module dependencies must not be admitted into shared contracts");
  assert_equal(static_cast<int>(InterfaceAdmissionDecision::Return),
               static_cast<int>(result.decision),
               "same-module dependencies must return to module scope");
  assert_equal(
      std::string("interface candidate must return to module scope because owner and primary consumer are not cross-module"),
      std::string(result.reason),
      "same-module dependencies must report the stable return reason");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Common test runner keeps this smoke executable aligned with the rest of
  // the contract-test entrypoints used across WP01-WP05.
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

  // Banner text keeps raw ctest output traceable back to the WP task.
  std::cout << "InterfaceAdmissionContractTest - WP05-T012-B\n";

  run_test("test_tool_manager_is_admitted", test_tool_manager_is_admitted);
  run_test("test_llm_adapter_is_admitted_and_baseline_count_is_four",
           test_llm_adapter_is_admitted_and_baseline_count_is_four);
  run_test("test_services_interfaces_are_admitted",
           test_services_interfaces_are_admitted);
  run_test("test_planner_is_postponed_until_supporting_contracts_freeze",
           test_planner_is_postponed_until_supporting_contracts_freeze);
  run_test("test_non_catalogued_interface_is_returned",
           test_non_catalogued_interface_is_returned);
  run_test("test_incomplete_metadata_is_returned",
           test_incomplete_metadata_is_returned);
  run_test("test_same_module_dependency_is_returned",
           test_same_module_dependency_is_returned);

  // Summary output follows the repository convention used by other smoke
  // contract tests, which keeps aggregated logs easy to scan.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}

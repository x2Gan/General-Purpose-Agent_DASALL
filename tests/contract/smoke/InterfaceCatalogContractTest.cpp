#include <exception>
#include <iostream>
#include <string>

#include "boundary/InterfaceCatalog.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::InterfaceAdmissionReadiness;
using dasall::contracts::InterfaceCandidate;
using dasall::contracts::InterfaceOwnerModule;
using dasall::contracts::count_interface_candidates_by_owner_module;
using dasall::contracts::count_interface_candidates_by_readiness;
using dasall::contracts::find_interface_catalog_entry;
using dasall::contracts::find_interface_catalog_entry_by_name;
using dasall::contracts::interface_admission_readiness_name;
using dasall::contracts::interface_candidate_name;
using dasall::contracts::interface_owner_module_name;
using dasall::contracts::interface_primary_consumer_name;
using dasall::contracts::is_review_ready_interface_candidate;
using dasall::contracts::kInterfaceCatalog;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Validates the catalog keeps the intended stage-5 scope and does not lose any
// retained cross-module candidate rows.
void test_catalog_keeps_only_retained_cross_module_candidates() {
  assert_equal(10,
               static_cast<int>(kInterfaceCatalog.size()),
               "interface catalog must retain the 10 stage-5 candidates");

  const auto* planner_entry =
      find_interface_catalog_entry(InterfaceCandidate::IPlanner);
  assert_true(planner_entry != nullptr,
              "IPlanner must exist in the interface catalog");
  assert_equal(std::string("cognition"),
               std::string(interface_owner_module_name(
                   planner_entry->owner_module)),
               "IPlanner must be owned by cognition");
}

// Validates the current mature set includes llm, tools, and the services pair
// after CAP-TODO-033 closed the shared-contract readiness review.
void test_review_ready_candidates_include_services_after_cap_033() {
  assert_equal(4,
               static_cast<int>(count_interface_candidates_by_readiness(
                   InterfaceAdmissionReadiness::ReviewReady)),
               "llm, tools, and services candidates should be review-ready after CAP-TODO-033");

  assert_true(is_review_ready_interface_candidate(
                  InterfaceCandidate::IToolManager),
              "IToolManager must be review-ready");
  assert_true(is_review_ready_interface_candidate(
                  InterfaceCandidate::ILLMAdapter),
              "ILLMAdapter must be review-ready");
  assert_true(is_review_ready_interface_candidate(
                  InterfaceCandidate::IExecutionService),
              "IExecutionService must be review-ready after CAP-TODO-033");
  assert_true(is_review_ready_interface_candidate(
                  InterfaceCandidate::IDataService),
              "IDataService must be review-ready after CAP-TODO-033");
  assert_true(!is_review_ready_interface_candidate(
                  InterfaceCandidate::IMemoryStore),
              "IMemoryStore must still wait for supporting contracts");
}

// Validates owner-module grouping remains traceable for future admission
// guards, especially the memory and services clusters that have multiple rows.
void test_owner_module_counts_match_design_groups() {
  assert_equal(2,
               static_cast<int>(count_interface_candidates_by_owner_module(
                   InterfaceOwnerModule::Memory)),
               "memory must own IMemoryStore and IContextOrchestrator only");
  assert_equal(2,
               static_cast<int>(count_interface_candidates_by_owner_module(
                   InterfaceOwnerModule::Services)),
               "services must own IExecutionService and IDataService only");
  assert_equal(2,
               static_cast<int>(count_interface_candidates_by_owner_module(
                   InterfaceOwnerModule::MultiAgent)),
               "multi_agent must own IAgentRegistry and IResultMerger only");
}

// Validates retained rows expose stable metadata strings that T012 can reuse
// instead of hard-coding ad hoc names or readiness labels.
void test_catalog_entry_metadata_is_queryable() {
  const auto* llm_entry =
      find_interface_catalog_entry_by_name("ILLMAdapter");
  assert_true(llm_entry != nullptr,
              "ILLMAdapter must be discoverable by canonical name");
  assert_equal(std::string("runtime"),
               std::string(interface_primary_consumer_name(
                   llm_entry->primary_consumer)),
               "ILLMAdapter must serve runtime as the primary consumer");
  assert_equal(std::string("review_ready"),
               std::string(interface_admission_readiness_name(
                   llm_entry->readiness)),
               "ILLMAdapter readiness label must stay stable");
}

// Negative coverage: platform, infra, and protocol-internal interfaces must be
// excluded from the shared contract-interface candidate catalog.
void test_internal_or_low_level_interfaces_are_excluded() {
  assert_true(find_interface_catalog_entry_by_name("IMCPAdapter") == nullptr,
              "IMCPAdapter must not enter the shared interface catalog");
  assert_true(find_interface_catalog_entry_by_name("ILogger") == nullptr,
              "ILogger must stay outside the shared interface catalog");
  assert_true(find_interface_catalog_entry_by_name("IGPIO") == nullptr,
              "IGPIO must stay outside the shared interface catalog");
}

// Negative coverage: unresolved candidates must preserve their awaiting state,
// while the services pair only flips after the explicit CAP-TODO-033 review.
void test_awaiting_candidates_keep_their_readiness_state() {
  const auto* merger_entry =
      find_interface_catalog_entry_by_name("IResultMerger");
  assert_true(merger_entry != nullptr,
              "IResultMerger must exist in the interface catalog");
  assert_equal(std::string("awaiting_supporting_contracts"),
               std::string(interface_admission_readiness_name(
                   merger_entry->readiness)),
               "IResultMerger must remain awaiting supporting contracts");
  const auto* execution_entry =
      find_interface_catalog_entry_by_name("IExecutionService");
  assert_true(execution_entry != nullptr,
              "IExecutionService must exist in the interface catalog");
  assert_equal(std::string("review_ready"),
               std::string(interface_admission_readiness_name(
                   execution_entry->readiness)),
               "IExecutionService must be review-ready after CAP-TODO-033");
  assert_equal(std::string("IResultMerger"),
               std::string(interface_candidate_name(
                   InterfaceCandidate::IResultMerger)),
               "candidate-name lookup must remain stable for IResultMerger");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Common test runner keeps this smoke executable consistent with the rest of
  // the repository contract-test entrypoints.
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

  // The banner keeps task traceability in raw test logs and ctest output.
  std::cout << "InterfaceCatalogContractTest - WP05-T011-B\n";

  run_test("test_catalog_keeps_only_retained_cross_module_candidates",
           test_catalog_keeps_only_retained_cross_module_candidates);
  run_test("test_review_ready_candidates_include_services_after_cap_033",
           test_review_ready_candidates_include_services_after_cap_033);
  run_test("test_owner_module_counts_match_design_groups",
           test_owner_module_counts_match_design_groups);
  run_test("test_catalog_entry_metadata_is_queryable",
           test_catalog_entry_metadata_is_queryable);
  run_test("test_internal_or_low_level_interfaces_are_excluded",
           test_internal_or_low_level_interfaces_are_excluded);
  run_test("test_awaiting_candidates_keep_their_readiness_state",
           test_awaiting_candidates_keep_their_readiness_state);

  // Summary output follows the existing contract-test convention for easier
  // scanning in aggregated logs.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}

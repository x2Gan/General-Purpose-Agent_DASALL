// ==========================================================================
// MultiAgentBoundaryContractsSmokeTest.cpp
//
// WP04-T013-B: Smoke test for MultiAgentBoundaryContracts.h.
//
// Validates that the ADR-008 aggregate entry point:
//   1. Exposes the four direct-impact objects captured by T013-D.
//   2. Preserves the layering anchors from top-level AgentRequest / AgentResult
//      down to collaboration-subdomain request/result and worker objects.
//   3. Re-exports the existing multi-agent boundary guards for request/result/
//      worker-task violations without redefining those semantics in a second
//      place.
//
// Verification command (WP04-T013):
//   cmake --build build-ci --target dasall_contract_tests
//   ctest --test-dir build-ci -R MultiAgentBoundaryContractsSmokeTest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>

#include "agent/MultiAgentBoundaryContracts.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::MultiAgentBoundaryDecision;
using dasall::contracts::MultiAgentBoundaryObject;
using dasall::contracts::evaluate_multi_agent_request_field_boundary;
using dasall::contracts::evaluate_multi_agent_result_field_boundary;
using dasall::contracts::evaluate_worker_task_field_boundary;
using dasall::contracts::is_multi_agent_boundary_object;
using dasall::contracts::is_runtime_controlled_multi_agent_object;
using dasall::contracts::kMultiAgentBoundaryObjectCatalog;
using dasall::contracts::multi_agent_boundary_object_name;
using dasall::contracts::multi_agent_boundary_upstream_anchor;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_catalog_contains_four_direct_impact_objects() {
  assert_equal(static_cast<int>(4),
               static_cast<int>(kMultiAgentBoundaryObjectCatalog.size()),
               "multi-agent boundary aggregate should expose exactly four T013 objects");

  assert_equal("MultiAgentRequest",
               std::string(multi_agent_boundary_object_name(
                   MultiAgentBoundaryObject::MultiAgentRequest)),
               "catalog should contain MultiAgentRequest");
  assert_equal("MultiAgentResult",
               std::string(multi_agent_boundary_object_name(
                   MultiAgentBoundaryObject::MultiAgentResult)),
               "catalog should contain MultiAgentResult");
  assert_equal("WorkerTask",
               std::string(multi_agent_boundary_object_name(
                   MultiAgentBoundaryObject::WorkerTask)),
               "catalog should contain WorkerTask");
  assert_equal("WorkerLease",
               std::string(multi_agent_boundary_object_name(
                   MultiAgentBoundaryObject::WorkerLease)),
               "catalog should contain WorkerLease");
}

void test_catalog_preserves_upstream_layering_anchors() {
  assert_equal("AgentRequest",
               std::string(multi_agent_boundary_upstream_anchor(
                   MultiAgentBoundaryObject::MultiAgentRequest)),
               "MultiAgentRequest should remain layered under AgentRequest");
  assert_equal("AgentResult",
               std::string(multi_agent_boundary_upstream_anchor(
                   MultiAgentBoundaryObject::MultiAgentResult)),
               "MultiAgentResult should remain layered under AgentResult");
  assert_equal("AgentOrchestrator task graph",
               std::string(multi_agent_boundary_upstream_anchor(
                   MultiAgentBoundaryObject::WorkerTask)),
               "WorkerTask should remain layered under the orchestrator-owned task graph");
  assert_equal("WorkerTask / top-level checkpoint subdomain snapshot",
               std::string(multi_agent_boundary_upstream_anchor(
                   MultiAgentBoundaryObject::WorkerLease)),
               "WorkerLease should remain layered under WorkerTask and checkpoint subdomain snapshot");

  assert_true(is_runtime_controlled_multi_agent_object(
                  MultiAgentBoundaryObject::MultiAgentRequest),
              "MultiAgentRequest must remain runtime-controlled under ADR-008");
  assert_true(is_runtime_controlled_multi_agent_object(
                  MultiAgentBoundaryObject::MultiAgentResult),
              "MultiAgentResult must remain runtime-controlled under ADR-008");
  assert_true(is_runtime_controlled_multi_agent_object(
                  MultiAgentBoundaryObject::WorkerTask),
              "WorkerTask must remain runtime-controlled under ADR-008");
  assert_true(is_runtime_controlled_multi_agent_object(
                  MultiAgentBoundaryObject::WorkerLease),
              "WorkerLease must remain runtime-controlled under ADR-008");
}

void test_name_lookup_accepts_only_multi_agent_wave_objects() {
  assert_true(is_multi_agent_boundary_object("MultiAgentRequest"),
              "MultiAgentRequest should be recognized by the aggregate header");
  assert_true(is_multi_agent_boundary_object("MultiAgentResult"),
              "MultiAgentResult should be recognized by the aggregate header");
  assert_true(is_multi_agent_boundary_object("WorkerTask"),
              "WorkerTask should be recognized by the aggregate header");
  assert_true(is_multi_agent_boundary_object("WorkerLease"),
              "WorkerLease should be recognized by the aggregate header");

  assert_true(!is_multi_agent_boundary_object("AgentRequest"),
              "AgentRequest is an upstream anchor and must not be duplicated into T013 scope");
  assert_true(!is_multi_agent_boundary_object("AgentResult"),
              "AgentResult is an upstream anchor and must not be duplicated into T013 scope");
  assert_true(!is_multi_agent_boundary_object("Checkpoint"),
              "top-level checkpoint is an upstream anchor and must not leak into T013 catalog");
}

void test_request_guard_is_visible_through_aggregate_include() {
  const auto allowed_result =
      evaluate_multi_agent_request_field_boundary("goal_fragment");
  assert_true(allowed_result.allowed,
              "goal_fragment should remain allowed through MultiAgentBoundaryContracts include");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::AllowField),
               static_cast<int>(allowed_result.decision),
               "allowed request field should keep allow decision");

  const auto rejected_result =
      evaluate_multi_agent_request_field_boundary("agent_request");
  assert_true(!rejected_result.allowed,
              "agent_request must remain rejected through MultiAgentBoundaryContracts include");
  assert_equal(static_cast<int>(
                   MultiAgentBoundaryDecision::RejectRequestReuseAgentRequest),
               static_cast<int>(rejected_result.decision),
               "request reuse field should keep request-reuse rejection decision");
}

void test_result_and_worker_task_guards_are_visible_through_aggregate_include() {
  const auto result_allowed =
      evaluate_multi_agent_result_field_boundary("merged_result");
  assert_true(result_allowed.allowed,
              "merged_result should remain allowed through MultiAgentBoundaryContracts include");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::AllowField),
               static_cast<int>(result_allowed.decision),
               "allowed result field should keep allow decision");

  const auto result_rejected =
      evaluate_multi_agent_result_field_boundary("agent_result");
  assert_true(!result_rejected.allowed,
              "agent_result must remain rejected through MultiAgentBoundaryContracts include");
  assert_equal(static_cast<int>(
                   MultiAgentBoundaryDecision::RejectResultReplaceAgentResult),
               static_cast<int>(result_rejected.decision),
               "result replacement field should keep result-replacement rejection decision");

  const auto worker_allowed = evaluate_worker_task_field_boundary("lease_id");
  assert_true(worker_allowed.allowed,
              "lease_id should remain allowed through MultiAgentBoundaryContracts include");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::AllowField),
               static_cast<int>(worker_allowed.decision),
               "allowed worker-task field should keep allow decision");

  const auto worker_rejected =
      evaluate_worker_task_field_boundary("global_fsm_state");
  assert_true(!worker_rejected.allowed,
              "global_fsm_state must remain rejected through MultiAgentBoundaryContracts include");
  assert_equal(static_cast<int>(
                   MultiAgentBoundaryDecision::RejectWorkerTaskGlobalState),
               static_cast<int>(worker_rejected.decision),
               "worker-task global-state field should keep worker-task rejection decision");
}

}  // namespace

int main() {
  try {
    test_catalog_contains_four_direct_impact_objects();
    test_catalog_preserves_upstream_layering_anchors();
    test_name_lookup_accepts_only_multi_agent_wave_objects();
    test_request_guard_is_visible_through_aggregate_include();
    test_result_and_worker_task_guards_are_visible_through_aggregate_include();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
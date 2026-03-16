#include <exception>
#include <iostream>
#include <string>

#include "boundary/MultiAgentBoundaryGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_multi_agent_request_subdomain_field_is_allowed() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_multi_agent_request_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_multi_agent_request_field_boundary("goal_fragment");

  // Positive case: collaboration-subdomain request fields should be allowed.
  assert_true(result.allowed,
              "goal_fragment should be allowed in MultiAgentRequest");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed request field should return allow decision");
}

void test_multi_agent_request_reuse_of_agent_request_is_rejected() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_multi_agent_request_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_multi_agent_request_field_boundary("agent_request");

  // Negative case: MultiAgentRequest must not reuse AgentRequest semantics.
  assert_true(!result.allowed,
              "agent_request must be rejected in MultiAgentRequest");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::RejectRequestReuseAgentRequest),
               static_cast<int>(result.decision),
               "request reuse should return request-reuse rejection decision");
}

void test_multi_agent_result_collaboration_field_is_allowed() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_multi_agent_result_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_multi_agent_result_field_boundary("merged_result");

  // Positive case: collaboration result fields should be allowed.
  assert_true(result.allowed,
              "merged_result should be allowed in MultiAgentResult");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed result field should return allow decision");
}

void test_multi_agent_result_replacing_agent_result_is_rejected() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_multi_agent_result_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_multi_agent_result_field_boundary("agent_result");

  // Negative case: MultiAgentResult must not replace top-level AgentResult.
  assert_true(!result.allowed,
              "agent_result must be rejected in MultiAgentResult");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::RejectResultReplaceAgentResult),
               static_cast<int>(result.decision),
               "result replacement should return result-replacement rejection decision");
}

void test_worker_task_execution_field_is_allowed() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_worker_task_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_worker_task_field_boundary("lease_id");

  // Positive case: WorkerTask execution-unit fields should be allowed.
  assert_true(result.allowed,
              "lease_id should be allowed in WorkerTask");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed worker-task field should return allow decision");
}

void test_worker_task_global_state_field_is_rejected() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_worker_task_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto result = evaluate_worker_task_field_boundary("global_fsm_state");

  // Negative case: WorkerTask must not carry top-level Session/FSM state.
  assert_true(!result.allowed,
              "global_fsm_state must be rejected in WorkerTask");
  assert_equal(static_cast<int>(MultiAgentBoundaryDecision::RejectWorkerTaskGlobalState),
               static_cast<int>(result.decision),
               "worker global state should return worker-task rejection decision");
  assert_equal("worker task must not carry global session or fsm state",
               std::string(result.reason),
               "worker-task rejection should return normalized reason");
}

void test_multi_agent_semantics_combination_regression_matrix() {
  using dasall::contracts::MultiAgentBoundaryDecision;
  using dasall::contracts::evaluate_multi_agent_request_field_boundary;
  using dasall::contracts::evaluate_multi_agent_result_field_boundary;
  using dasall::contracts::evaluate_worker_task_field_boundary;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  struct RequestCase {
    const char* field_name;
    bool expected_allowed;
    MultiAgentBoundaryDecision expected_decision;
    const char* expected_reason;
  };

  struct ResultCase {
    const char* field_name;
    bool expected_allowed;
    MultiAgentBoundaryDecision expected_decision;
    const char* expected_reason;
  };

  struct WorkerTaskCase {
    const char* field_name;
    bool expected_allowed;
    MultiAgentBoundaryDecision expected_decision;
    const char* expected_reason;
  };

  // Positive matrix: one legal field per layer verifies ADR-008 layering.
  constexpr RequestCase kRequestPositiveCase{
      "goal_fragment",
      true,
      MultiAgentBoundaryDecision::AllowField,
      "multi-agent boundary field is allowed by ADR-008",
  };
  constexpr ResultCase kResultPositiveCase{
      "merged_result",
      true,
      MultiAgentBoundaryDecision::AllowField,
      "multi-agent boundary field is allowed by ADR-008",
  };
  constexpr WorkerTaskCase kWorkerTaskPositiveCase{
      "lease_id",
      true,
      MultiAgentBoundaryDecision::AllowField,
      "multi-agent boundary field is allowed by ADR-008",
  };

  const auto request_positive_result =
      evaluate_multi_agent_request_field_boundary(kRequestPositiveCase.field_name);
  const auto result_positive_result =
      evaluate_multi_agent_result_field_boundary(kResultPositiveCase.field_name);
  const auto worker_task_positive_result =
      evaluate_worker_task_field_boundary(kWorkerTaskPositiveCase.field_name);

  assert_true(request_positive_result.allowed == kRequestPositiveCase.expected_allowed,
              "positive request combination should be allowed");
  assert_equal(static_cast<int>(kRequestPositiveCase.expected_decision),
               static_cast<int>(request_positive_result.decision),
               "positive request combination should keep allow decision");
  assert_equal(std::string(kRequestPositiveCase.expected_reason),
               std::string(request_positive_result.reason),
               "positive request combination should keep normalized allow reason");

  assert_true(result_positive_result.allowed == kResultPositiveCase.expected_allowed,
              "positive result combination should be allowed");
  assert_equal(static_cast<int>(kResultPositiveCase.expected_decision),
               static_cast<int>(result_positive_result.decision),
               "positive result combination should keep allow decision");
  assert_equal(std::string(kResultPositiveCase.expected_reason),
               std::string(result_positive_result.reason),
               "positive result combination should keep normalized allow reason");

  assert_true(worker_task_positive_result.allowed ==
                  kWorkerTaskPositiveCase.expected_allowed,
              "positive worker-task combination should be allowed");
  assert_equal(static_cast<int>(kWorkerTaskPositiveCase.expected_decision),
               static_cast<int>(worker_task_positive_result.decision),
               "positive worker-task combination should keep allow decision");
  assert_equal(std::string(kWorkerTaskPositiveCase.expected_reason),
               std::string(worker_task_positive_result.reason),
               "positive worker-task combination should keep normalized allow reason");

  // Negative matrix: one violation per layer enforces request/result/worker
  // separation required by WP01-B009 and ADR-008.
  constexpr RequestCase kRequestNegativeCases[] = {
      {"agent_request",
       false,
       MultiAgentBoundaryDecision::RejectRequestReuseAgentRequest,
       "multi-agent request must not reuse agent-request semantics"},
  };
  constexpr ResultCase kResultNegativeCases[] = {
      {"agent_result",
       false,
       MultiAgentBoundaryDecision::RejectResultReplaceAgentResult,
       "multi-agent result must not replace top-level agent result"},
  };
  constexpr WorkerTaskCase kWorkerTaskNegativeCases[] = {
      {"global_fsm_state",
       false,
       MultiAgentBoundaryDecision::RejectWorkerTaskGlobalState,
       "worker task must not carry global session or fsm state"},
  };

  for (const auto& request_case : kRequestNegativeCases) {
    const auto result =
        evaluate_multi_agent_request_field_boundary(request_case.field_name);
    assert_true(result.allowed == request_case.expected_allowed,
                "negative request combination should be rejected");
    assert_equal(static_cast<int>(request_case.expected_decision),
                 static_cast<int>(result.decision),
                 "negative request combination should map to request-reuse rejection");
    assert_equal(std::string(request_case.expected_reason),
                 std::string(result.reason),
                 "negative request combination should return normalized rejection reason");
  }

  for (const auto& result_case : kResultNegativeCases) {
    const auto result =
        evaluate_multi_agent_result_field_boundary(result_case.field_name);
    assert_true(result.allowed == result_case.expected_allowed,
                "negative result combination should be rejected");
    assert_equal(static_cast<int>(result_case.expected_decision),
                 static_cast<int>(result.decision),
                 "negative result combination should map to result-replacement rejection");
    assert_equal(std::string(result_case.expected_reason),
                 std::string(result.reason),
                 "negative result combination should return normalized rejection reason");
  }

  for (const auto& worker_task_case : kWorkerTaskNegativeCases) {
    const auto result =
        evaluate_worker_task_field_boundary(worker_task_case.field_name);
    assert_true(result.allowed == worker_task_case.expected_allowed,
                "negative worker-task combination should be rejected");
    assert_equal(static_cast<int>(worker_task_case.expected_decision),
                 static_cast<int>(result.decision),
                 "negative worker-task combination should map to global-state rejection");
    assert_equal(std::string(worker_task_case.expected_reason),
                 std::string(result.reason),
                 "negative worker-task combination should return normalized rejection reason");
  }
}

}  // namespace

int main() {
  try {
    test_multi_agent_request_subdomain_field_is_allowed();
    test_multi_agent_request_reuse_of_agent_request_is_rejected();
    test_multi_agent_result_collaboration_field_is_allowed();
    test_multi_agent_result_replacing_agent_result_is_rejected();
    test_worker_task_execution_field_is_allowed();
    test_worker_task_global_state_field_is_rejected();
    test_multi_agent_semantics_combination_regression_matrix();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

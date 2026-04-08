// ==========================================================================
// MainFlowOverlapContractTest.cpp
//
// WP03-T016-B: Contract smoke test for MainFlowOverlapGuards.h.
// Validates that:
//   - Legitimate main-flow fields pass the overlap guard (positive cases)
//   - Fields from 5 forbidden domains are rejected (negative cases)
//   - Symmetric exclusion between adjacent object pairs works
//   - Domain assignment completeness holds
//
// Test structure:
//   Positive (4):
//     1. Legitimate field passes global overlap guard
//     2. Domain assignment completeness (8 objects, 8 domains)
//     3. Observation field passes Observation-not-Digest check
//     4. Checkpoint field passes Checkpoint-not-BeliefState check
//   Negative (5):
//     1. Prompt domain field rejected
//     2. Recovery domain field rejected
//     3. Worker domain field rejected
//     4. Digest-only field rejected in Observation context
//     5. BeliefState-only field rejected in Checkpoint context
//
// Verification command:
//   cmake --build build-ci --target dasall_contract_tests && \
//   ctest --test-dir build-ci -R MainFlowOverlapContractTest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>

#include "boundary/MainFlowOverlapGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::MainFlowOverlapDecision;
using dasall::contracts::MainFlowOverlapResult;
using dasall::contracts::evaluate_main_flow_overlap;
using dasall::contracts::is_allowed_main_flow_field;
using dasall::contracts::is_belief_state_not_checkpoint_field;
using dasall::contracts::is_checkpoint_not_belief_state_field;
using dasall::contracts::is_digest_not_observation_field;
using dasall::contracts::is_observation_not_digest_field;
using dasall::contracts::verify_domain_assignment_completeness;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// =========================================================================
// Positive case 1: Legitimate field passes global overlap guard
// =========================================================================
void test_legitimate_field_passes_overlap_guard() {
  // "request_id" is a standard correlation field used by multiple objects
  // and must never be rejected by the overlap guard.
  const auto result = evaluate_main_flow_overlap("AgentRequest", "request_id");
  assert_true(result.allowed,
              "request_id should pass overlap guard for AgentRequest");
  assert_equal(static_cast<int>(MainFlowOverlapDecision::AllowField),
               static_cast<int>(result.decision),
               "allowed field should return AllowField decision");

  // Also test with another object and another legitimate field.
  const auto result2 = evaluate_main_flow_overlap("Observation", "observation_id");
  assert_true(result2.allowed,
              "observation_id should pass overlap guard for Observation");

  // And a third: BeliefState with "confirmed_facts"
  const auto result3 = evaluate_main_flow_overlap("BeliefState", "confirmed_facts");
  assert_true(result3.allowed,
              "confirmed_facts should pass overlap guard for BeliefState");
}

// =========================================================================
// Positive case 2: Domain assignment completeness
// =========================================================================
void test_domain_assignment_completeness() {
  // verify_domain_assignment_completeness() checks kMainFlowObjectNames
  // and kMainFlowObjectDomains both have exactly 8 entries.
  assert_true(verify_domain_assignment_completeness(),
              "domain assignment must cover all 8 main-flow objects");

  // Additionally verify the first and last entries are correct.
  assert_equal(std::string("AgentRequest"),
               std::string(dasall::contracts::kMainFlowObjectNames[0]),
               "first object should be AgentRequest");
  assert_equal(std::string("AgentResult"),
               std::string(dasall::contracts::kMainFlowObjectNames[7]),
               "last object should be AgentResult");
  assert_equal(std::string("AccessLayer"),
               std::string(dasall::contracts::kMainFlowObjectDomains[0]),
               "AgentRequest should be in AccessLayer");
  assert_equal(std::string("AccessLayer"),
               std::string(dasall::contracts::kMainFlowObjectDomains[7]),
               "AgentResult should be in AccessLayer");
}

// =========================================================================
// Positive case 3: Observation field passes Observation-not-Digest check
// =========================================================================
void test_observation_field_passes_symmetric_check() {
  // "payload" is an Observation-only field; it should pass the
  // "is this field NOT a Digest field?" check.
  assert_true(is_observation_not_digest_field("payload"),
              "payload should be accepted as Observation (not Digest) field");

  // "observation_id" is shared; it should also pass (not a Digest-only field).
  assert_true(is_observation_not_digest_field("observation_id"),
              "observation_id should pass Observation-not-Digest check");
}

// =========================================================================
// Positive case 4: Checkpoint field passes Checkpoint-not-BeliefState check
// =========================================================================
void test_checkpoint_field_passes_symmetric_check() {
  // "state" is a Checkpoint-only field.
  assert_true(is_checkpoint_not_belief_state_field("state"),
              "state should be accepted as Checkpoint (not BeliefState) field");

  // "step_id" is also Checkpoint-only.
  assert_true(is_checkpoint_not_belief_state_field("step_id"),
              "step_id should be accepted as Checkpoint (not BeliefState) field");
}

// =========================================================================
// Negative case 1: Prompt domain field rejected
// =========================================================================
void test_prompt_domain_field_rejected() {
  const auto result = evaluate_main_flow_overlap("ContextPacket", "final_messages");
  assert_true(!result.allowed,
              "final_messages must be rejected as Prompt domain field");
  assert_equal(static_cast<int>(MainFlowOverlapDecision::RejectPromptDomain),
               static_cast<int>(result.decision),
               "should return RejectPromptDomain decision");
  assert_equal(std::string("ContextPacket"),
               std::string(result.object_name),
               "result should carry the object name");

  // Also test rendered_prompt against AgentRequest
  const auto result2 = evaluate_main_flow_overlap("AgentRequest", "rendered_prompt");
  assert_true(!result2.allowed,
              "rendered_prompt must be rejected for AgentRequest");
}

// =========================================================================
// Negative case 2: Recovery domain field rejected
// =========================================================================
void test_recovery_domain_field_rejected() {
  const auto result = evaluate_main_flow_overlap("Observation", "retry_after_ms");
  assert_true(!result.allowed,
              "retry_after_ms must be rejected as Recovery domain field");
  assert_equal(static_cast<int>(MainFlowOverlapDecision::RejectRecoveryDomain),
               static_cast<int>(result.decision),
               "should return RejectRecoveryDomain decision");

  // Also test backoff_strategy
  const auto result2 = evaluate_main_flow_overlap("BeliefState", "backoff_strategy");
  assert_true(!result2.allowed,
              "backoff_strategy must be rejected for BeliefState");
}

// =========================================================================
// Negative case 3: Worker domain field rejected
// =========================================================================
void test_worker_domain_field_rejected() {
  const auto result = evaluate_main_flow_overlap("AgentResult", "worker_results");
  assert_true(!result.allowed,
              "worker_results must be rejected as Worker domain field");
  assert_equal(static_cast<int>(MainFlowOverlapDecision::RejectWorkerDomain),
               static_cast<int>(result.decision),
               "should return RejectWorkerDomain decision");

  // Also test worker_task_id against AgentRequest
  const auto result2 = evaluate_main_flow_overlap("AgentRequest", "worker_task_id");
  assert_true(!result2.allowed,
              "worker_task_id must be rejected for AgentRequest");
}

// =========================================================================
// Negative case 4: Digest-only field rejected in Observation context
// =========================================================================
void test_digest_field_rejected_in_observation_context() {
  // "summary" is a Digest-only field; it must NOT appear in Observation.
  assert_true(!is_observation_not_digest_field("summary"),
              "summary must be rejected as Digest-only field in Observation context");

  // "confidence" is also Digest-only.
  assert_true(!is_observation_not_digest_field("confidence"),
              "confidence must be rejected as Digest-only field in Observation context");

  // "key_facts" is also Digest-only.
  assert_true(!is_observation_not_digest_field("key_facts"),
              "key_facts must be rejected as Digest-only field in Observation context");
}

// =========================================================================
// Negative case 5: BeliefState-only field rejected in Checkpoint context
// =========================================================================
void test_belief_state_field_rejected_in_checkpoint_context() {
  // "confirmed_facts" is BeliefState-only; it must NOT appear in Checkpoint.
  assert_true(!is_checkpoint_not_belief_state_field("confirmed_facts"),
              "confirmed_facts must be rejected in Checkpoint context");

  // "hypotheses" is also BeliefState-only.
  assert_true(!is_checkpoint_not_belief_state_field("hypotheses"),
              "hypotheses must be rejected in Checkpoint context");

  // "assumptions" is also BeliefState-only.
  assert_true(!is_checkpoint_not_belief_state_field("assumptions"),
              "assumptions must be rejected in Checkpoint context");
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

  std::cout << "MainFlowOverlapContractTest — WP03-T016-B\n";

  // Positive cases
  run_test("test_legitimate_field_passes_overlap_guard",
           test_legitimate_field_passes_overlap_guard);
  run_test("test_domain_assignment_completeness",
           test_domain_assignment_completeness);
  run_test("test_observation_field_passes_symmetric_check",
           test_observation_field_passes_symmetric_check);
  run_test("test_checkpoint_field_passes_symmetric_check",
           test_checkpoint_field_passes_symmetric_check);

  // Negative cases
  run_test("test_prompt_domain_field_rejected",
           test_prompt_domain_field_rejected);
  run_test("test_recovery_domain_field_rejected",
           test_recovery_domain_field_rejected);
  run_test("test_worker_domain_field_rejected",
           test_worker_domain_field_rejected);
  run_test("test_digest_field_rejected_in_observation_context",
           test_digest_field_rejected_in_observation_context);
  run_test("test_belief_state_field_rejected_in_checkpoint_context",
           test_belief_state_field_rejected_in_checkpoint_context);

  std::cout << "\nResults: " << passed << " passed, " << failed << " failed, "
            << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}

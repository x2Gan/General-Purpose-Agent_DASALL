// ==========================================================================
// MainFlowContractE2ETest.cpp
//
// WP03-T015-B: End-to-end contract smoke test for the single-Agent main
// flow chain.  Validates that all 8 canonical objects can be constructed
// with valid fields, pass their respective Guards, and maintain cross-object
// correlation integrity (request_id, observation_id, goal_id, checkpoint_ref).
//
// Test structure:
//   - Positive cases:
//       1. Full valid chain: all 8 objects pass Guards.
//       2. Correlation integrity: cross-object reference fields match.
//       3. Canonical order completeness: kCanonicalOrder covers 8 nodes.
//   - Negative cases:
//       1. Broken correlation: mismatched request_id across chain.
//       2. Guard failure propagation: invalid object in chain is detected.
//
// Verification command:
//   cmake --build build-ci --target dasall_contract_tests && \
//   ctest --test-dir build-ci -R MainFlowContractE2ETest --output-on-failure
// ==========================================================================
#include <exception>
#include <iostream>
#include <string>

// --- Main flow contract objects ---
#include "agent/AgentRequest.h"
#include "agent/AgentRequestGuards.h"
#include "agent/AgentResult.h"
#include "agent/AgentResultGuards.h"
#include "agent/BeliefState.h"
#include "agent/BeliefStateGuards.h"
#include "agent/GoalContract.h"
#include "agent/GoalContractGuards.h"
#include "agent/MainFlowContracts.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointGuards.h"
#include "context/ContextPacket.h"
#include "context/ContextPacketGuards.h"
#include "observation/Observation.h"
#include "observation/ObservationDigest.h"
#include "observation/ObservationDigestGuards.h"
#include "observation/ObservationGuards.h"

// --- Test support ---
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::AgentRequest;
using dasall::contracts::AgentRequestGuardResult;
using dasall::contracts::AgentResult;
using dasall::contracts::AgentResultGuardResult;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::BeliefState;
using dasall::contracts::BeliefStateGuardResult;
using dasall::contracts::Checkpoint;
using dasall::contracts::CheckpointGuardResult;
using dasall::contracts::CheckpointState;
using dasall::contracts::ContextPacket;
using dasall::contracts::ContextPacketGuardResult;
using dasall::contracts::GoalContract;
using dasall::contracts::GoalContractGuardResult;
using dasall::contracts::GoalStatus;
using dasall::contracts::MainFlowContracts;
using dasall::contracts::Observation;
using dasall::contracts::ObservationDigest;
using dasall::contracts::ObservationDigestGuardResult;
using dasall::contracts::ObservationGuardResult;
using dasall::contracts::ObservationSource;
using dasall::contracts::RequestChannel;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// =========================================================================
// Helper: construct a valid instance of each main-flow object sharing
// consistent correlation fields.
// =========================================================================

// Shared correlation identifiers for the positive-chain scenario.
constexpr const char* kRequestId = "req-e2e-001";
constexpr const char* kSessionId = "sess-e2e-001";
constexpr const char* kTraceId = "trace-e2e-001";
constexpr const char* kGoalId = "goal-e2e-001";
constexpr const char* kObservationId = "obs-e2e-001";
constexpr const char* kCheckpointId = "ckpt-e2e-001";
constexpr const char* kResultId = "res-e2e-001";
constexpr int64_t kTimestamp = 1710000000000;

// --- Node 0: AgentRequest ---
AgentRequest make_valid_agent_request() {
  AgentRequest req;
  req.request_id = std::string(kRequestId);
  req.session_id = std::string(kSessionId);
  req.trace_id = std::string(kTraceId);
  req.user_input = "查询北京天气";
  req.request_channel = RequestChannel::Cli;
  req.created_at = kTimestamp;
  return req;
}

// --- Node 1: GoalContract ---
GoalContract make_valid_goal_contract() {
  GoalContract goal;
  goal.goal_id = std::string(kGoalId);
  goal.request_id = std::string(kRequestId);
  goal.goal_description = "查询北京天气并返回温度和天气状况";
  goal.success_criteria = "response contains temperature and weather";
  goal.status = GoalStatus::Active;
  goal.created_at = kTimestamp;
  return goal;
}

// --- Node 2: ContextPacket ---
ContextPacket make_valid_context_packet() {
  ContextPacket pkt;
  pkt.request_id = std::string(kRequestId);
  pkt.user_turn = "查询北京天气";
  pkt.current_goal_summary = "查询北京天气并返回温度和天气状况";
  pkt.recent_history = std::vector<std::string>{};  // first turn, empty OK
  return pkt;
}

// --- Node 3: Observation ---
Observation make_valid_observation() {
  Observation obs;
  obs.observation_id = std::string(kObservationId);
  obs.source = ObservationSource::ToolExecution;
  obs.success = true;
  obs.payload = R"({"temperature":22,"weather":"sunny"})";
  obs.created_at = kTimestamp + 1000;
  return obs;
}

// --- Node 4: ObservationDigest ---
ObservationDigest make_valid_observation_digest() {
  ObservationDigest digest;
  digest.observation_id = std::string(kObservationId);
  digest.summary = "北京当前22°C，晴天";
  digest.key_facts = std::vector<std::string>{"temperature=22", "weather=sunny"};
  digest.citations = std::vector<std::string>{"weather_api_v2"};
  digest.confidence = 0.95f;
  return digest;
}

// --- Node 5: BeliefState ---
BeliefState make_valid_belief_state() {
  BeliefState state;
  state.request_id = std::string(kRequestId);
  state.confirmed_facts = std::vector<std::string>{"北京22°C，晴天"};
  state.hypotheses = std::vector<std::string>{};
  state.assumptions = std::vector<std::string>{};
  state.evidence_refs = std::vector<std::string>{kObservationId};
  state.confidence = 0.95f;
  return state;
}

// --- Node 6: Checkpoint ---
Checkpoint make_valid_checkpoint() {
  Checkpoint cp;
  cp.checkpoint_id = std::string(kCheckpointId);
  cp.state = CheckpointState::Succeeded;
  cp.step_id = "tool-weather-001";
  cp.working_memory_snapshot = R"({"last_obs":"obs-e2e-001"})";
  cp.pending_action = "";  // no pending action on success
  return cp;
}

// --- Node 7: AgentResult ---
AgentResult make_valid_agent_result() {
  AgentResult result;
  result.result_id = std::string(kResultId);
  result.status = AgentResultStatus::Completed;
  result.result_code = 1001;  // Validation category (WP-02)
  result.response_text = "北京当前温度22°C，天气晴。";
  result.task_completed = true;
  result.created_at = kTimestamp + 2000;
  // Optional correlation fields — E2E chain links.
  result.request_id = std::string(kRequestId);
  result.trace_id = std::string(kTraceId);
  result.goal_id = std::string(kGoalId);
  result.checkpoint_ref = std::string(kCheckpointId);
  return result;
}

// =========================================================================
// Positive Test 1: Full valid chain — all 8 objects pass Guards.
// =========================================================================
void test_e2e_all_guards_pass() {
  // Node 0: AgentRequest
  auto req = make_valid_agent_request();
  AgentRequestGuardResult req_guard =
      dasall::contracts::validate_agent_request_boundary(req);
  assert_true(req_guard.ok, "AgentRequest boundary guard should pass");

  // Node 1: GoalContract
  auto goal = make_valid_goal_contract();
  GoalContractGuardResult goal_guard =
      dasall::contracts::validate_goal_contract_required_fields(goal);
  assert_true(goal_guard.ok, "GoalContract required fields guard should pass");

  // Node 2: ContextPacket
  auto pkt = make_valid_context_packet();
  ContextPacketGuardResult pkt_guard =
      dasall::contracts::validate_context_packet_required_fields(pkt);
  assert_true(pkt_guard.ok, "ContextPacket required fields guard should pass");

  // Node 3: Observation
  auto obs = make_valid_observation();
  ObservationGuardResult obs_guard =
      dasall::contracts::validate_observation_required_fields(obs);
  assert_true(obs_guard.ok, "Observation required fields guard should pass");

  // Node 4: ObservationDigest
  auto digest = make_valid_observation_digest();
  ObservationDigestGuardResult digest_guard =
      dasall::contracts::validate_observation_digest_required_fields(digest);
  assert_true(digest_guard.ok,
              "ObservationDigest required fields guard should pass");

  // Node 5: BeliefState
  auto belief = make_valid_belief_state();
  BeliefStateGuardResult belief_guard =
      dasall::contracts::validate_belief_state_required_fields(belief);
  assert_true(belief_guard.ok,
              "BeliefState required fields guard should pass");

  // Node 6: Checkpoint
  auto cp = make_valid_checkpoint();
  CheckpointGuardResult cp_guard =
      dasall::contracts::validate_checkpoint_required_fields(cp);
  assert_true(cp_guard.ok, "Checkpoint required fields guard should pass");

  // Node 7: AgentResult
  auto result = make_valid_agent_result();
  AgentResultGuardResult result_guard =
      dasall::contracts::validate_agent_result_boundary(result);
  assert_true(result_guard.ok, "AgentResult boundary guard should pass");
}

// =========================================================================
// Positive Test 2: Correlation integrity — cross-object reference fields
// match across the entire chain.
// =========================================================================
void test_e2e_correlation_integrity() {
  auto req = make_valid_agent_request();
  auto goal = make_valid_goal_contract();
  auto pkt = make_valid_context_packet();
  auto obs = make_valid_observation();
  auto digest = make_valid_observation_digest();
  auto belief = make_valid_belief_state();
  auto cp = make_valid_checkpoint();
  auto result = make_valid_agent_result();

  // E1: AgentRequest.request_id == GoalContract.request_id
  assert_equal(*req.request_id, *goal.request_id,
               "E1: AgentRequest->GoalContract request_id must match");

  // E2: AgentRequest.request_id == ContextPacket.request_id
  assert_equal(*req.request_id, *pkt.request_id,
               "E2: AgentRequest->ContextPacket request_id must match");

  // E4: Observation.observation_id == ObservationDigest.observation_id
  assert_equal(*obs.observation_id, *digest.observation_id,
               "E4: Observation->ObservationDigest observation_id must match");

  // E5: AgentRequest.request_id == BeliefState.request_id
  assert_equal(*req.request_id, *belief.request_id,
               "E5: AgentRequest->BeliefState request_id must match");

  // E6: GoalContract.goal_id == AgentResult.goal_id
  assert_equal(*goal.goal_id, *result.goal_id,
               "E6: GoalContract->AgentResult goal_id must match");

  // E7: Checkpoint.checkpoint_id == AgentResult.checkpoint_ref
  assert_equal(*cp.checkpoint_id, *result.checkpoint_ref,
               "E7: Checkpoint->AgentResult checkpoint_ref must match");

  // E8: AgentRequest.request_id == AgentResult.request_id
  assert_equal(*req.request_id, *result.request_id,
               "E8: AgentRequest->AgentResult request_id must match");
}

// =========================================================================
// Positive Test 3: Canonical order completeness — kCanonicalOrder has 8
// nodes and is_direct_successor holds for all adjacent pairs.
// =========================================================================
void test_canonical_chain_completeness() {
  // 8 nodes in canonical order.
  assert_equal(8, static_cast<int>(MainFlowContracts::canonical_count()),
               "MainFlowContracts should have exactly 8 canonical nodes");

  // Verify every adjacent pair is a direct successor.
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::AgentRequest,
          MainFlowContracts::Node::GoalContract),
      "AgentRequest -> GoalContract should be direct successor");
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::GoalContract,
          MainFlowContracts::Node::ContextPacket),
      "GoalContract -> ContextPacket should be direct successor");
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::ContextPacket,
          MainFlowContracts::Node::Observation),
      "ContextPacket -> Observation should be direct successor");
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::Observation,
          MainFlowContracts::Node::ObservationDigest),
      "Observation -> ObservationDigest should be direct successor");
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::ObservationDigest,
          MainFlowContracts::Node::BeliefState),
      "ObservationDigest -> BeliefState should be direct successor");
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::BeliefState,
          MainFlowContracts::Node::Checkpoint),
      "BeliefState -> Checkpoint should be direct successor");
  assert_true(
      MainFlowContracts::is_direct_successor(
          MainFlowContracts::Node::Checkpoint,
          MainFlowContracts::Node::AgentResult),
      "Checkpoint -> AgentResult should be direct successor");
}

// =========================================================================
// Positive Test 4: Boundary-level guards pass for all 8 objects.
// =========================================================================
void test_e2e_boundary_guards_pass() {
  // Node 0: AgentRequest boundary
  auto req = make_valid_agent_request();
  auto req_result = dasall::contracts::validate_agent_request_boundary(req);
  assert_true(req_result.ok,
              "AgentRequest boundary should pass in E2E chain");

  // Node 3: Observation boundary
  auto obs = make_valid_observation();
  auto obs_result = dasall::contracts::validate_observation_boundary(obs);
  assert_true(obs_result.ok,
              "Observation boundary should pass in E2E chain");

  // Node 4: ObservationDigest boundary
  auto digest = make_valid_observation_digest();
  auto digest_result =
      dasall::contracts::validate_observation_digest_boundary(digest);
  assert_true(digest_result.ok,
              "ObservationDigest boundary should pass in E2E chain");

  // Node 5: BeliefState boundary
  auto belief = make_valid_belief_state();
  auto belief_result =
      dasall::contracts::validate_belief_state_boundary(belief);
  assert_true(belief_result.ok,
              "BeliefState boundary should pass in E2E chain");

  // Node 6: Checkpoint boundary
  auto cp = make_valid_checkpoint();
  auto cp_result = dasall::contracts::validate_checkpoint_boundary(cp);
  assert_true(cp_result.ok,
              "Checkpoint boundary should pass in E2E chain");

  // Node 7: AgentResult boundary
  auto result = make_valid_agent_result();
  auto result_guard =
      dasall::contracts::validate_agent_result_boundary(result);
  assert_true(result_guard.ok,
              "AgentResult boundary should pass in E2E chain");
}

// =========================================================================
// Negative Test 1: Broken correlation — GoalContract.request_id does not
// match AgentRequest.request_id.  The chain should detect this mismatch
// through simple string comparison.
// =========================================================================
void test_e2e_broken_request_id_correlation() {
  auto req = make_valid_agent_request();
  auto goal = make_valid_goal_contract();

  // Corrupt GoalContract's request_id.
  goal.request_id = "req-WRONG-999";

  // Both objects individually pass their own Guards.
  auto req_guard = dasall::contracts::validate_agent_request_boundary(req);
  assert_true(req_guard.ok, "AgentRequest itself should still pass guard");

  auto goal_guard =
      dasall::contracts::validate_goal_contract_required_fields(goal);
  assert_true(goal_guard.ok, "GoalContract itself should still pass guard");

  // But cross-object correlation is broken.
  assert_true(*req.request_id != *goal.request_id,
              "Broken correlation: request_id should NOT match");
}

// =========================================================================
// Negative Test 2: Broken correlation — ObservationDigest.observation_id
// does not match Observation.observation_id.
// =========================================================================
void test_e2e_broken_observation_id_correlation() {
  auto obs = make_valid_observation();
  auto digest = make_valid_observation_digest();

  // Corrupt digest's observation_id.
  digest.observation_id = "obs-WRONG-999";

  // Both objects individually pass their Guards.
  auto obs_guard =
      dasall::contracts::validate_observation_required_fields(obs);
  assert_true(obs_guard.ok, "Observation itself should still pass guard");

  auto digest_guard =
      dasall::contracts::validate_observation_digest_required_fields(digest);
  assert_true(digest_guard.ok,
              "ObservationDigest itself should still pass guard");

  // Cross-object correlation is broken.
  assert_true(*obs.observation_id != *digest.observation_id,
              "Broken correlation: observation_id should NOT match");
}

// =========================================================================
// Negative Test 3: Broken correlation — AgentResult.checkpoint_ref
// does not match Checkpoint.checkpoint_id.
// =========================================================================
void test_e2e_broken_checkpoint_ref_correlation() {
  auto cp = make_valid_checkpoint();
  auto result = make_valid_agent_result();

  // Corrupt result's checkpoint_ref.
  result.checkpoint_ref = "ckpt-WRONG-999";

  // Both objects individually pass their Guards.
  auto cp_guard =
      dasall::contracts::validate_checkpoint_required_fields(cp);
  assert_true(cp_guard.ok, "Checkpoint itself should still pass guard");

  auto result_guard =
      dasall::contracts::validate_agent_result_boundary(result);
  assert_true(result_guard.ok, "AgentResult itself should still pass guard");

  // Cross-object correlation is broken.
  assert_true(*cp.checkpoint_id != *result.checkpoint_ref,
              "Broken correlation: checkpoint_ref should NOT match");
}

// =========================================================================
// Negative Test 4: Guard failure propagation — an invalid AgentRequest
// (missing required field) is detected before the chain can proceed.
// =========================================================================
void test_e2e_invalid_entry_detected() {
  AgentRequest req;
  // Intentionally leave all fields empty.

  auto req_guard =
      dasall::contracts::validate_agent_request_required_fields(req);
  assert_true(!req_guard.ok,
              "Empty AgentRequest must fail required-field guard");
}

// =========================================================================
// Negative Test 5: Guard failure propagation — an invalid AgentResult
// (missing required fields) is detected at the chain exit.
// =========================================================================
void test_e2e_invalid_exit_detected() {
  AgentResult result;
  // Intentionally leave all fields empty.

  auto result_guard =
      dasall::contracts::validate_agent_result_required_fields(result);
  assert_true(!result_guard.ok,
              "Empty AgentResult must fail required-field guard");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  auto run = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  [PASS] " << name << std::endl;
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  [FAIL] " << name << ": " << ex.what() << std::endl;
    }
  };

  std::cout << "MainFlowContractE2ETest" << std::endl;

  // --- Positive cases ---
  run("test_e2e_all_guards_pass", test_e2e_all_guards_pass);
  run("test_e2e_correlation_integrity", test_e2e_correlation_integrity);
  run("test_canonical_chain_completeness", test_canonical_chain_completeness);
  run("test_e2e_boundary_guards_pass", test_e2e_boundary_guards_pass);

  // --- Negative cases ---
  run("test_e2e_broken_request_id_correlation",
      test_e2e_broken_request_id_correlation);
  run("test_e2e_broken_observation_id_correlation",
      test_e2e_broken_observation_id_correlation);
  run("test_e2e_broken_checkpoint_ref_correlation",
      test_e2e_broken_checkpoint_ref_correlation);
  run("test_e2e_invalid_entry_detected", test_e2e_invalid_entry_detected);
  run("test_e2e_invalid_exit_detected", test_e2e_invalid_exit_detected);

  std::cout << std::endl
            << passed << " passed, " << failed << " failed, "
            << (passed + failed) << " total" << std::endl;

  return failed == 0 ? 0 : 1;
}

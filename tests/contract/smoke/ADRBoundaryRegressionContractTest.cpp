#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/MultiAgentResult.h"
#include "agent/MultiAgentResultGuards.h"
#include "boundary/ADRFieldMappingGuards.h"
#include "checkpoint/ReflectionDecision.h"
#include "checkpoint/ReflectionDecisionGuards.h"
#include "context/ContextPacket.h"
#include "context/ContextPacketGuards.h"
#include "support/TestAssertions.h"
#include "prompt/PromptBoundaryContracts.h"

namespace {

using dasall::contracts::ADRIdentifier;
using dasall::contracts::ADRMappedObject;
using dasall::contracts::ContextPacket;
using dasall::contracts::MultiAgentResult;
using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::contracts::evaluate_context_packet_prompt_field_boundary;
using dasall::contracts::has_adr_forbidden_field_mapping;
using dasall::contracts::is_adr_object_mapped;
using dasall::contracts::validate_context_packet_field_rules;
using dasall::contracts::validate_multi_agent_result_field_rules;
using dasall::contracts::validate_multi_agent_result_forbidden_field;
using dasall::contracts::validate_reflection_decision_contract_field_boundary;
using dasall::contracts::validate_reflection_decision_field_rules;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds a valid ContextPacket sample to prove ADR-006 positive path remains
// compatible with frozen field-level guard rules.
ContextPacket make_valid_context_packet_sample() {
  ContextPacket packet;
  packet.request_id = "req-adr-006-001";
  packet.user_turn = "Summarize this release checkpoint.";
  packet.current_goal_summary = "produce concise release readiness summary";
  packet.recent_history = std::vector<std::string>{
      "User asked for release checklist.",
      "Agent collected prior CI outcomes.",
  };
  packet.summary_memory = "recent release notes and gate evidence";
  packet.retrieval_evidence =
      std::vector<std::string>{"evidence://ci/gate", "evidence://adr/006"};
  packet.latest_observation_digest_summary =
      "latest contract test run passed";
  packet.active_tools = std::vector<std::string>{"ctest", "cmake"};
  packet.policy_digest = "no direct provider payload injection";
  packet.token_budget_report = "context=900, prompt=500, reserve=200";
  packet.belief_state_summary = "release-risk is low";
  packet.created_at = 1710000000300;
  packet.tags = std::vector<std::string>{"wp05", "adr-boundary"};
  return packet;
}

// Builds a valid ReflectionDecision sample to prove ADR-007 positive path
// still supports suggestion-only semantics with frozen field rules.
ReflectionDecision make_valid_reflection_decision_sample() {
  ReflectionDecision decision;
  decision.request_id = "req-adr-007-001";
  decision.decision_kind = ReflectionDecisionKind::Replan;
  decision.rationale = "missing evidence path requires controlled replan";
  decision.goal_id = "goal-adr-007";
  decision.confidence = 0.86F;
  decision.relevant_observation_refs =
      std::vector<std::string>{"obs://failure/timeout", "obs://queue/backlog"};
  decision.hint_ref = "hint://replan-with-smaller-scope";
  decision.created_at = 1710000000400;
  decision.tags = std::vector<std::string>{"adr-007", "reflection"};
  return decision;
}

// Builds a valid MultiAgentResult sample to prove ADR-008 positive path keeps
// collaboration-result semantics and does not collapse into AgentResult.
MultiAgentResult make_valid_multi_agent_result_sample() {
  MultiAgentResult result;
  result.subtask_results = std::vector<std::string>{
      "worker-a: extracted config drift delta",
      "worker-b: verified contract test history",
  };
  result.merged_result = "consolidated release recommendation";
  result.recommended_next_action = "fold_into_agent_result";
  result.conflicts = std::vector<std::string>{"none-critical"};
  result.worker_trace_refs =
      std::vector<std::string>{"trace://worker-a", "trace://worker-b"};
  result.failure_summary = "worker-c skipped due to no-op branch";
  return result;
}

// Positive coverage: ContextPacket with legal semantic fields must remain
// guard-valid so ADR-006 boundary checks do not regress into over-rejection.
void test_context_packet_positive_path_passes_field_rules() {
  const auto packet = make_valid_context_packet_sample();
  const auto guard = validate_context_packet_field_rules(packet);
  assert_true(guard.ok,
              "valid ContextPacket sample should pass field rules");
}

// Negative coverage: ADR-006 forbids message-layer fields inside
// ContextPacket, so rendered_prompt must always be rejected.
void test_context_packet_message_field_boundary_is_rejected() {
  const auto boundary =
      evaluate_context_packet_prompt_field_boundary("rendered_prompt");
  assert_true(!boundary.allowed,
              "ContextPacket must reject rendered_prompt boundary field");
}

// Positive coverage: ReflectionDecision suggestion object should pass field
// rules when only legal cognition-side fields are present.
void test_reflection_decision_positive_path_passes_field_rules() {
  const auto decision = make_valid_reflection_decision_sample();
  const auto guard = validate_reflection_decision_field_rules(decision);
  assert_true(guard.ok,
              "valid ReflectionDecision sample should pass field rules");
}

// Negative coverage: ADR-007 scheduling fields must not enter
// ReflectionDecision and should be rejected by the boundary wrapper.
void test_reflection_decision_scheduling_field_is_rejected() {
  const auto boundary =
      validate_reflection_decision_contract_field_boundary("retry_after_ms");
  assert_true(!boundary.allowed,
              "ReflectionDecision must reject retry_after_ms boundary field");
}

// Positive coverage: MultiAgentResult must keep collaboration semantics and
// pass field rules when no AgentResult replacement aliases are present.
void test_multi_agent_result_positive_path_passes_field_rules() {
  const auto result = make_valid_multi_agent_result_sample();
  const auto guard = validate_multi_agent_result_field_rules(result);
  assert_true(guard.ok,
              "valid MultiAgentResult sample should pass field rules");
}

// Negative coverage: ADR-008 forbids MultiAgentResult from carrying
// top-level AgentResult replacement fields such as agent_result.
void test_multi_agent_result_replacement_field_is_rejected() {
  const auto boundary = validate_multi_agent_result_forbidden_field(
      "agent_result");
  assert_true(!boundary.ok,
              "MultiAgentResult must reject agent_result boundary field");
}

// Regression coverage: ADR mapping catalog must keep key object ownership and
// representative forbidden-field mappings for the three target objects.
void test_adr_mapping_catalog_still_covers_target_objects_and_fields() {
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR006,
                                   ADRMappedObject::ContextPacket),
              "ADR-006 must map ContextPacket");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR007,
                                   ADRMappedObject::ReflectionDecision),
              "ADR-007 must map ReflectionDecision");
  assert_true(is_adr_object_mapped(ADRIdentifier::ADR008,
                                   ADRMappedObject::MultiAgentResult),
              "ADR-008 must map MultiAgentResult");

  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR006,
                                              ADRMappedObject::ContextPacket,
                                              "rendered_prompt"),
              "ADR-006 must map ContextPacket.rendered_prompt");
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR007,
                                              ADRMappedObject::ReflectionDecision,
                                              "retry_after_ms"),
              "ADR-007 must map ReflectionDecision.retry_after_ms");
  assert_true(has_adr_forbidden_field_mapping(ADRIdentifier::ADR008,
                                              ADRMappedObject::MultiAgentResult,
                                              "agent_result"),
              "ADR-008 must map MultiAgentResult.agent_result");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Shared runner keeps output format aligned with existing smoke tests so
  // CI logs remain easy to scan and task-traceable.
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

  // Banner text maps ctest output directly to the WP05-T016-B task.
  std::cout << "ADRBoundaryRegressionContractTest - WP05-T016-B\n";

  run_test("test_context_packet_positive_path_passes_field_rules",
           test_context_packet_positive_path_passes_field_rules);
  run_test("test_context_packet_message_field_boundary_is_rejected",
           test_context_packet_message_field_boundary_is_rejected);
  run_test("test_reflection_decision_positive_path_passes_field_rules",
           test_reflection_decision_positive_path_passes_field_rules);
  run_test("test_reflection_decision_scheduling_field_is_rejected",
           test_reflection_decision_scheduling_field_is_rejected);
  run_test("test_multi_agent_result_positive_path_passes_field_rules",
           test_multi_agent_result_positive_path_passes_field_rules);
  run_test("test_multi_agent_result_replacement_field_is_rejected",
           test_multi_agent_result_replacement_field_is_rejected);
  run_test("test_adr_mapping_catalog_still_covers_target_objects_and_fields",
           test_adr_mapping_catalog_still_covers_target_objects_and_fields);

  // Summary output follows the repository convention used by other contract
  // tests so aggregated logs stay consistent.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}

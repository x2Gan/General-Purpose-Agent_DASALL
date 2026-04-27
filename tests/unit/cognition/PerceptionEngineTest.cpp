#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "perception/PerceptionEngine.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionStepRequest;
using dasall::cognition::perception::PerceptionEngine;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool has_entity(const std::vector<dasall::cognition::perception::EntityCandidate>& entities,
                              const std::string& name,
                              const std::string& value) {
  for (const auto& entity : entities) {
    if (entity.name == name && entity.value == value) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool has_diagnostic(const std::vector<std::string>& diagnostics,
                                  const std::string& expected) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] CognitionStepRequest make_request(std::string user_turn,
                                                std::optional<std::vector<std::string>> active_tools,
                                                float belief_confidence,
                                                std::vector<std::string> hypotheses = {},
                                                std::vector<std::string> evidence_refs = {std::string("belief:evidence")}) {
  CognitionStepRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-014";
  request.trace_id = "trace-014";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-014");
  request.goal_contract.request_id = std::string("req-014");
  request.goal_contract.goal_description = std::string("collect evidence for quarterly sales in Berlin");
  request.goal_contract.success_criteria = std::string("return verifiable evidence for the requested city");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;
  request.goal_contract.constraints = std::string("stay within builtin tools");

  request.context_packet.request_id = std::string("req-014");
  request.context_packet.user_turn = std::move(user_turn);
  request.context_packet.current_goal_summary = std::string("find evidence for Berlin sales");
  request.context_packet.recent_history =
      std::vector<std::string>{std::string("user asked for quarterly sales evidence")};
  request.context_packet.active_tools = std::move(active_tools);
  request.context_packet.policy_digest = std::string("builtin-only policy");

  request.belief_state.request_id = std::string("req-014");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses = std::move(hypotheses);
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("builtin dataset contains quarterly sales")};
  request.belief_state.evidence_refs = std::move(evidence_refs);
  request.belief_state.confidence = belief_confidence;
  request.belief_state.goal_id = std::string("goal-014");
  request.belief_state.created_at = 1712345600100;

  return request;
}

void test_perceive_extracts_entities_constraints_and_actionable_mode() {
  PerceptionEngine engine(dasall::cognition::CognitionConfig{});

  const auto result = engine.perceive(make_request(
      "Search dataset for quarterly sales in Berlin", std::vector<std::string>{"agent.dataset"},
      0.90F));

  assert_true(result.has_value(), "complete actionable input should yield a PerceptionResult");
  assert_equal(std::string("action_decision"), result->task_type,
               "perception should classify lookup-style requests as action_decision");
  assert_true(has_entity(result->entities, "goal", "collect evidence for quarterly sales in Berlin"),
              "perception should keep the goal description as an extracted entity");
  assert_true(has_entity(result->entities, "tool", "agent.dataset"),
              "perception should surface visible tools as entity candidates");
  assert_equal(1, static_cast<int>(result->constraints_digest.hard_constraints.size()),
               "goal constraints should project into hard constraints");
  assert_equal(1, static_cast<int>(result->constraints_digest.policy_refs.size()),
               "policy digest should project into policy refs");
  assert_true(!result->requires_clarification,
              "fully specified actionable input should not trigger clarification");
  assert_true(has_diagnostic(result->diagnostics, "perception.actionable"),
              "actionable perception output should record an actionable diagnostic");
}

void test_perceive_detects_ambiguity_and_returns_clarification_questions() {
  PerceptionEngine engine(dasall::cognition::CognitionConfig{});

  const auto result = engine.perceive(make_request(
      "Help me with it maybe", std::nullopt, 0.25F,
      std::vector<std::string>{std::string("the target may be a dataset refresh")}, {}));

  assert_true(result.has_value(), "rule fallback should still yield a perception result for ambiguous input");
  assert_true(result->requires_clarification,
              "underspecified target input should require clarification");
  assert_true(!result->clarification_questions.empty(),
              "ambiguous input should surface at least one clarification question");
  assert_true(result->confidence < 0.45F,
              "ambiguous input should lower perception confidence below the clarification threshold");
  assert_true(has_diagnostic(result->diagnostics, "perception.requires_clarification"),
              "clarification path should emit the matching diagnostic");
}

}  // namespace

int main() {
  try {
    test_perceive_extracts_entities_constraints_and_actionable_mode();
    test_perceive_detects_ambiguity_and_returns_clarification_questions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
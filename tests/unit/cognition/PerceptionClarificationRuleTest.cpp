#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "perception/PerceptionEngine.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::CognitionStepRequest;
using dasall::cognition::perception::PerceptionEngine;
using dasall::tests::support::assert_true;

[[nodiscard]] CognitionStepRequest make_request() {
  CognitionStepRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-014-rule";
  request.trace_id = "trace-014-rule";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-014-rule");
  request.goal_contract.request_id = std::string("req-014-rule");
  request.goal_contract.goal_description = std::string("help the user resolve the next action");
  request.goal_contract.success_criteria = std::string("produce a safe next step");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345601000;

  request.context_packet.request_id = std::string("req-014-rule");
  request.context_packet.user_turn = std::string("it");
  request.context_packet.current_goal_summary = std::string("help the user move forward");
  request.context_packet.recent_history = std::vector<std::string>{std::string("previous turn referenced an unknown target")};

  request.belief_state.request_id = std::string("req-014-rule");
  request.belief_state.confirmed_facts = std::vector<std::string>{};
  request.belief_state.hypotheses = std::vector<std::string>{std::string("it may refer to a missing object")};
  request.belief_state.assumptions = std::vector<std::string>{std::string("runtime still needs a concrete target")};
  request.belief_state.evidence_refs = std::vector<std::string>{};
  request.belief_state.confidence = 0.10F;
  request.belief_state.goal_id = std::string("goal-014-rule");
  request.belief_state.created_at = 1712345601100;

  return request;
}

void test_rule_fallback_disabled_rejects_unresolved_pronoun_only_request() {
  CognitionConfig config;
  config.perception.rule_fallback_enabled = false;
  PerceptionEngine engine(config);

  const auto result = engine.perceive(make_request());

  assert_true(!result.has_value(),
              "when rule fallback is disabled, pronoun-only ambiguous input should fail closed");
}

void test_rule_fallback_enabled_keeps_conservative_clarification_path_alive() {
  PerceptionEngine engine(CognitionConfig{});

  const auto result = engine.perceive(make_request());

  assert_true(result.has_value(),
              "rule fallback should keep a conservative clarification path alive");
  assert_true(result->requires_clarification,
              "rule fallback should prefer clarification for unresolved pronoun-only input");
  assert_true(!result->clarification_questions.empty(),
              "rule fallback should emit a clarification question instead of inventing a concrete target");
}

}  // namespace

int main() {
  try {
    test_rule_fallback_disabled_rejects_unresolved_pronoun_only_request();
    test_rule_fallback_enabled_keeps_conservative_clarification_path_alive();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <string>

#include "belief/BeliefUpdateSynthesizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::belief::BeliefMergeMode;
using dasall::cognition::belief::BeliefUpdateSynthesizer;
using dasall::cognition::decision::ActionDecision;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::decision::ToolIntentHint;
using dasall::cognition::perception::EntityCandidate;
using dasall::cognition::perception::PerceptionResult;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_fact(
    const dasall::cognition::belief::BeliefUpdateHint& hint,
    const std::string& expected) {
  for (const auto& delta : hint.confirmed_facts_delta) {
    if (delta.fact == expected) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool contains_hypothesis(
    const dasall::cognition::belief::BeliefUpdateHint& hint,
    const std::string& expected) {
  for (const auto& delta : hint.hypotheses_delta) {
    if (delta.hypothesis == expected) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool contains_evidence_ref(
    const dasall::cognition::belief::BeliefUpdateHint& hint,
    const std::string& expected) {
  for (const auto& delta : hint.evidence_refs_delta) {
    if (delta.evidence_ref == expected) {
      return true;
    }
  }
  return false;
}

void test_synthesize_from_decide_collects_verified_facts_and_tool_hypothesis() {
  BeliefUpdateSynthesizer synthesizer;

  PerceptionResult perception_result;
  perception_result.intent_summary = "collect verified quarterly sales evidence";
  perception_result.task_type = "action_decision";
  perception_result.entities = {
      EntityCandidate{.name = "city",
                      .value = "Berlin",
                      .confidence = 0.93F,
                      .evidence_refs = {std::string("entity:city:berlin")}},
      EntityCandidate{.name = "metric",
                      .value = "quarterly_sales",
                      .confidence = 0.89F,
                      .evidence_refs = {std::string("entity:metric:quarterly_sales")}},
  };
  perception_result.confidence = 0.86F;

  ActionDecision action_decision;
  action_decision.decision_kind = ActionDecisionKind::ExecuteAction;
  action_decision.selected_node_id = std::string("plan-018-node");
  action_decision.confidence = 0.84F;
  action_decision.tool_intent_hint = ToolIntentHint{
      .tool_name = "agent.dataset",
      .intent_summary = "query quarterly sales dataset",
      .argument_hints = {std::string("city=Berlin")},
      .evidence_refs = {std::string("entity:city:berlin"), std::string("plan:018:node")},
  };

  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-018-decide");
  observation.success = true;
  observation.payload = std::string("verified quarterly sales result returned");
  observation.created_at = 1712345602000;

  const auto hint =
      synthesizer.synthesize_from_decide(perception_result, action_decision, observation);

  assert_true(hint.merge_mode == BeliefMergeMode::Append,
              "successful decide path should prefer append merge mode");
  assert_true(contains_fact(hint, "city=Berlin"),
              "high-confidence entities with evidence should become confirmed fact deltas");
  assert_true(contains_fact(hint, "observation:verified quarterly sales result returned"),
              "successful observations should contribute verified observation facts");
  assert_true(contains_hypothesis(hint, "pending_tool:agent.dataset"),
              "selected tool intents should be preserved as best-effort hypotheses");
  assert_true(contains_evidence_ref(hint, "obs-018-decide") &&
                  contains_evidence_ref(hint, "plan:018:node"),
              "decide path should retain observation and tool evidence refs");
  assert_true(hint.confidence_hint.has_value() && *hint.confidence_hint >= 0.89F,
              "successful decide path should surface a strong confidence hint");
}

}  // namespace

int main() {
  try {
    test_synthesize_from_decide_collects_verified_facts_and_tool_hypothesis();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
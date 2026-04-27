#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "belief/BeliefUpdateSynthesizer.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::belief::BeliefDeltaKind;
using dasall::cognition::belief::BeliefMergeMode;
using dasall::cognition::belief::BeliefUpdateHint;
using dasall::cognition::belief::BeliefUpdateSynthesizer;
using dasall::contracts::ReflectionDecision;
using dasall::contracts::ReflectionDecisionKind;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_synthesize_from_reflection_retracts_invalidated_assumptions_and_uses_replace() {
  BeliefUpdateSynthesizer synthesizer;

  ReflectionDecision reflection_decision;
  reflection_decision.request_id = std::string("req-018-replan");
  reflection_decision.decision_kind = ReflectionDecisionKind::Replan;
  reflection_decision.rationale = std::string(
      "reflection detected invalidated assumptions: dataset access is available; "
      "source schema matches the current extraction template; failure_source=assumption_or_environment_shift");
  reflection_decision.confidence = 0.78F;
  reflection_decision.relevant_observation_refs =
      std::vector<std::string>{std::string("obs-018-replan")};

  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-018-replan");
  belief_state.confirmed_facts = std::vector<std::string>{std::string("Berlin is the target city")};
  belief_state.hypotheses = std::vector<std::string>{std::string("dataset lookup should work")};
  belief_state.assumptions = std::vector<std::string>{
      std::string("dataset access is available"),
      std::string("source schema matches the current extraction template"),
  };
  belief_state.evidence_refs = std::vector<std::string>{std::string("belief:evidence:replan")};
  belief_state.confidence = 0.74F;

  dasall::contracts::Observation observation;
  observation.observation_id = std::string("obs-018-replan");
  observation.success = false;
  observation.payload = std::string("dataset access is not available and schema changed");
  observation.created_at = 1712345603000;

  const auto hint =
      synthesizer.synthesize_from_reflection(reflection_decision, belief_state, observation);

  assert_true(hint.merge_mode == BeliefMergeMode::Replace,
              "replan reflections should ask Runtime/Memory for replace-style merge mode");
  assert_equal(2, static_cast<int>(hint.assumptions_delta.size()),
               "replan reflections should retract all explicitly invalidated assumptions");
  assert_true(hint.assumptions_delta.front().delta_kind == BeliefDeltaKind::Retract &&
                  hint.assumptions_delta.back().delta_kind == BeliefDeltaKind::Retract,
              "invalidated assumptions should be emitted as retract deltas");
  assert_true(hint.confidence_hint.has_value() && *hint.confidence_hint == 0.78F,
              "reflection confidence should flow into the belief update hint");
}

void test_merge_deltas_escalates_to_strongest_merge_mode() {
  BeliefUpdateSynthesizer synthesizer;

  BeliefUpdateHint append_hint;
  append_hint.confirmed_facts_delta = {
      {.fact = "city=Berlin", .delta_kind = BeliefDeltaKind::Upsert},
  };
  append_hint.evidence_refs_delta = {
      {.evidence_ref = "entity:city:berlin", .delta_kind = BeliefDeltaKind::Upsert},
  };
  append_hint.confidence_hint = 0.65F;
  append_hint.merge_mode = BeliefMergeMode::Append;

  BeliefUpdateHint replace_hint;
  replace_hint.assumptions_delta = {
      {.assumption = "dataset access is available", .delta_kind = BeliefDeltaKind::Retract},
  };
  replace_hint.evidence_refs_delta = {
      {.evidence_ref = "obs-018-replan", .delta_kind = BeliefDeltaKind::Upsert},
  };
  replace_hint.confidence_hint = 0.82F;
  replace_hint.merge_mode = BeliefMergeMode::Replace;

  const auto merged = synthesizer.merge_deltas({append_hint, replace_hint});

  assert_true(merged.merge_mode == BeliefMergeMode::Replace,
              "merge_deltas should preserve the strongest merge mode across hints");
  assert_true(merged.confidence_hint.has_value() && *merged.confidence_hint == 0.82F,
              "merge_deltas should preserve the strongest confidence hint");
  assert_equal(1, static_cast<int>(merged.confirmed_facts_delta.size()),
               "merged hint should keep append facts");
  assert_equal(1, static_cast<int>(merged.assumptions_delta.size()),
               "merged hint should keep replace assumptions");
}

}  // namespace

int main() {
  try {
    test_synthesize_from_reflection_retracts_invalidated_assumptions_and_uses_replace();
    test_merge_deltas_escalates_to_strongest_merge_mode();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
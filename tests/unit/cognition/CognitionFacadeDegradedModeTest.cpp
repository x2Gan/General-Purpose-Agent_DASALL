#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "CognitionConfig.h"
#include "ICognitionEngine.h"
#include "decision/ActionDecision.h"
#include "support/TestAssertions.h"
#include "tests/mocks/include/MockCognitionFixture.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::create_cognition_engine;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

void test_cognition_facade_degrades_to_clarification_when_perception_cannot_route() {
  CognitionConfig config;
  config.perception.rule_fallback_enabled = false;

  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .user_turn = "it",
  });
  auto engine = create_cognition_engine(config);
  auto request = fixture.make_decide_request(false);
  request.context_packet.active_tools.reset();
  request.belief_state.confidence = 0.20F;
  request.execution_hints.degraded_path_allowed = true;

  const auto result = engine->decide(request);

  assert_true(!result.result_code.has_value(),
              "degraded decide flow should stay fail-open when degradation is allowed");
  assert_true(result.action_decision.has_value(),
              "degraded decide flow should still return a bounded action decision");
  assert_equal(static_cast<int>(ActionDecisionKind::AskClarification),
               static_cast<int>(result.action_decision->decision_kind),
               "perception routing gaps should degrade into ask_clarification");
  assert_true(result.action_decision->clarification_needed,
              "degraded clarification path should set clarification_needed");
  assert_true(result.context_sufficiency.recommend_context_reload,
              "degraded clarification path should recommend context reload");
  assert_true(!result.context_sufficiency.context_sufficient,
              "degraded clarification path should mark context as insufficient");
  assert_true(contains_value(result.context_sufficiency.missing_evidence_hints,
                             "context_packet.user_turn"),
              "degraded clarification path should surface missing evidence hints");
  assert_true(result.belief_update_hint.has_value(),
              "degraded clarification path should surface a belief update hint");
  assert_true(contains_value(result.belief_update_hint->missing_evidence_refs,
                             "context_packet.user_turn"),
              "degraded clarification path should preserve the missing evidence ref");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.degraded"),
              "degraded path should stamp the pipeline degradation diagnostic");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.perception_unavailable"),
              "degraded path should expose the perception failure reason");
}

}  // namespace

int main() {
  try {
    test_cognition_facade_degrades_to_clarification_when_perception_cannot_route();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
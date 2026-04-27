#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "agent/AgentResult.h"
#include "checkpoint/ReflectionDecision.h"
#include "decision/ActionDecision.h"
#include "support/TestAssertions.h"
#include "tests/mocks/include/MockCognitionFixture.h"

namespace {

using dasall::cognition::create_cognition_engine;
using dasall::cognition::create_response_builder;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::ReflectionDecisionKind;
using dasall::tests::mocks::MockCognitionFixture;
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

void test_cognition_facade_orchestrates_decide_reflect_and_response_flow() {
  MockCognitionFixture fixture;
  auto engine = create_cognition_engine();
  auto builder = create_response_builder();

  const auto decision_result = engine->decide(fixture.make_decide_request(true));

  assert_true(!decision_result.result_code.has_value(),
              "valid decide flow should not emit a result code");
  assert_true(decision_result.action_decision.has_value(),
              "valid decide flow should synthesize an action decision");
  assert_equal(static_cast<int>(ActionDecisionKind::ExecuteAction),
               static_cast<int>(decision_result.action_decision->decision_kind),
               "facade should route actionable requests to execute_action");
  assert_true(decision_result.belief_update_hint.has_value(),
              "valid decide flow should project a belief update hint");
  assert_true(!decision_result.belief_update_hint->confirmed_facts_delta.empty(),
              "belief update hint should confirm at least one fact after decide");
  assert_true(decision_result.context_sufficiency.context_sufficient,
              "clear actionable requests should retain sufficient context");
  assert_true(contains_value(decision_result.diagnostics, "decision_pipeline.completed"),
              "decide flow should stamp a pipeline completion diagnostic");

  const auto reflection_result =
      engine->reflect(fixture.make_reflection_request(fixture.make_observation(true)));

  assert_true(!reflection_result.result_code.has_value(),
              "valid reflection flow should not emit a result code");
  assert_true(reflection_result.reflection_decision.has_value(),
              "valid reflection flow should return a reflection decision");
  assert_true(reflection_result.reflection_decision->decision_kind.has_value(),
              "reflection decision should carry a concrete decision kind");
  assert_equal(static_cast<int>(ReflectionDecisionKind::Continue),
               static_cast<int>(*reflection_result.reflection_decision->decision_kind),
               "successful observations should keep the reflection decision on continue");
  assert_true(reflection_result.belief_update_hint.has_value(),
              "reflection flow should synthesize a belief update hint");
  assert_true(contains_value(reflection_result.diagnostics, "reflection_pipeline.completed"),
              "reflection flow should stamp a pipeline completion diagnostic");

  const auto response_result = builder->build(
      fixture.make_response_request(*decision_result.action_decision, fixture.make_observation(true)));

  assert_true(!response_result.result_code.has_value(),
              "response builder should accept the facade-composed terminal flow");
  assert_true(response_result.agent_result.has_value(),
              "response builder should produce an agent result for a complete flow");
  assert_true(response_result.agent_result->status.has_value(),
              "response builder should stamp the terminal agent status");
  assert_equal(static_cast<int>(AgentResultStatus::Completed),
               static_cast<int>(*response_result.agent_result->status),
               "response builder should stay on the llm projection path when observation payload exists");
  assert_true(!response_result.fallback_used,
              "response builder should avoid template fallback in the happy path");
  assert_true(contains_value(response_result.diagnostics, "response_mode:llm_projection"),
              "response builder should mark the llm projection response mode");
}

}  // namespace

int main() {
  try {
    test_cognition_facade_orchestrates_decide_reflect_and_response_flow();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
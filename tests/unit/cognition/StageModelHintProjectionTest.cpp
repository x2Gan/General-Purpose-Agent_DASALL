#include <exception>
#include <iostream>
#include <string>

#include "llm/CognitionLlmBridge.h"
#include "MockLLMManager.h"
#include "route/ModelSelectionHint.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ModelCapabilityTier;
using dasall::cognition::StageModelHint;
using dasall::cognition::llm_bridge::CognitionLlmBridge;
using dasall::cognition::llm_bridge::StageLlmCallRequest;
using dasall::cognition::llm_bridge::StageSchemaKind;
using dasall::cognition::llm_bridge::StageSchemaSpec;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] StageLlmCallRequest make_call_request() {
  StageLlmCallRequest request;
  request.request_id = "req-020-stage-hint";
  request.trace_id = "trace-020-stage-hint";
  request.stage_name = "response";
  request.task_type = "final_response";
  request.messages = {
      "system: produce a concise final response",
      "user: return the final answer",
  };
  request.model_hint = StageModelHint{
      .stage_name = "response",
      .task_type = "final_response",
      .capability_tier = ModelCapabilityTier::Standard,
      .max_output_tokens = 320U,
      .deadline_ms = 3200U,
      .requires_structured_output = false,
      .requires_reasoning_trace = false,
      .cost_sensitivity = 0.60F,
      .preferred_provider = {},
  };
  request.schema_spec = StageSchemaSpec{
      .schema_kind = StageSchemaKind::Text,
      .output_schema_ref = {},
      .allow_plain_text_fallback = true,
  };
  return request;
}

void test_build_llm_request_projects_canonical_stage_model_hint() {
  CognitionLlmBridge bridge(std::make_shared<MockLLMManager>());
  const auto request = bridge.build_llm_request(make_call_request());

  assert_equal(std::string("response"), request.stage,
               "response builder must project the canonical response stage key");
  assert_equal(std::string("final_response"), request.task_type,
               "bridge should preserve the response task_type projection");
  assert_true(!request.request.model_route.has_value(),
              "bridge must not manufacture a private model route alias");
  assert_true(request.request.response_format.has_value() &&
                  *request.request.response_format == "text",
              "natural-language response stages should stay on the text response format");
  assert_true(!request.request.output_schema_ref.has_value(),
              "non-structured response stages should not emit a schema ref");
  assert_true(request.selection_hint != nullptr,
              "stage model hints should always project into llm selection hints");
  assert_equal(std::string("response"), request.selection_hint->stage,
               "selection hint stage must stay on the canonical response key");
  assert_equal(std::string("standard"), request.selection_hint->complexity_tier,
               "standard capability tier should project to the standard complexity lane");
  assert_equal(std::string("background"), request.selection_hint->latency_sla_tier,
               "slow response deadlines should map to the background latency lane");
  assert_equal(std::string("unspecified"), request.selection_hint->budget_tier,
               "missing budget context should keep the budget lane unspecified");
  assert_true(!request.selection_hint->requires_reasoning,
              "final response stage should not request reasoning traces");
  assert_true(!request.selection_hint->requires_tools,
              "final response stage should not advertise tool usage to llm routing");
  assert_true(request.selection_hint->target_output_tokens == 320U,
              "selection hint should keep the declared response output budget");
}

}  // namespace

int main() {
  try {
    test_build_llm_request_projects_canonical_stage_model_hint();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
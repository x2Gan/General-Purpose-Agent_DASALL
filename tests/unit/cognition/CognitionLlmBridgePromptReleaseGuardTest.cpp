#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "llm/CognitionLlmBridge.h"
#include "MockLLMManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ModelCapabilityTier;
using dasall::cognition::StageModelHint;
using dasall::cognition::llm_bridge::CognitionLlmBridge;
using dasall::cognition::llm_bridge::StageLlmCallRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::PromptEvalStatus;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_token(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] StageLlmCallRequest make_call_request() {
  StageLlmCallRequest request;
  request.request_id = "req-020-release-guard";
  request.trace_id = "trace-020-release-guard";
  request.stage_name = "execution";
  request.task_type = "action_decision";
  request.messages = {
      "system: produce a governed action decision",
      "user: choose a safe next step",
  };
  request.model_hint = StageModelHint{
      .stage_name = "execution",
      .task_type = "action_decision",
      .capability_tier = ModelCapabilityTier::Standard,
      .max_output_tokens = 256U,
      .deadline_ms = 1200U,
      .requires_structured_output = true,
      .requires_reasoning_trace = false,
      .cost_sensitivity = 0.40F,
      .preferred_provider = {},
  };
  return request;
}

[[nodiscard]] dasall::llm::LLMManagerResult make_prompt_release_result(
    const PromptEvalStatus eval_status,
    std::string release_scope) {
  LLMResponse response;
  response.request_id = std::string{"req-020-release-guard"};
  response.llm_call_id = std::string{"mock-llm-call"};
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = std::string{"{\"decision\":\"direct\"}"};
  response.completed_at = 1712746800000LL;
  response.model_name = std::string{"mock.model"};
  response.prompt_id = std::string{"mock.prompt"};
  response.prompt_version = std::string{"v1"};
  response.eval_status = eval_status;
  response.release_scope = std::move(release_scope);
  response.finish_reason = std::string{"stop"};
  response.input_tokens = 16U;
  response.output_tokens = 8U;
  response.total_tokens = 24U;

  dasall::llm::LLMManagerResult result;
  result.response = std::move(response);
  result.resolved_route = "llm.exec.primary";
  result.attempted_routes = std::vector<std::string>{result.resolved_route};
  return result;
}

void test_invoke_stage_maps_retired_prompt_release_to_policy_denied() {
  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_result(
      make_prompt_release_result(PromptEvalStatus::Deprecated, "desktop_full"));

  CognitionLlmBridge bridge(llm_manager);
  const auto result = bridge.invoke_stage(make_call_request());

  assert_true(!result.response.has_value(),
              "retired prompt releases must not materialize a successful normalized response");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == dasall::contracts::ResultCode::PolicyDenied,
              "retired prompt releases should map to PolicyDenied");
  assert_true(result.error_info.has_value(),
              "retired prompt releases should surface an ErrorInfo payload");
  assert_equal(std::string("execution"), result.error_info->details.stage,
               "retired prompt releases should be rewritten onto the canonical cognition stage");
  assert_true(contains_token(result.diagnostics, "llm_failure:prompt_governance"),
              "retired prompt releases should surface prompt governance diagnostics");
  assert_true(contains_token(result.diagnostics, "prompt_retired"),
              "retired prompt releases should emit the prompt_retired diagnostic token");
  assert_true(contains_token(result.diagnostics, "error_type:policy"),
              "retired prompt releases should project the policy error category");
}

void test_invoke_stage_maps_blocked_prompt_eval_to_policy_denied() {
  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_result(
      make_prompt_release_result(PromptEvalStatus::Stable, "eval_blocked"));

  CognitionLlmBridge bridge(llm_manager);
  const auto result = bridge.invoke_stage(make_call_request());

  assert_true(!result.response.has_value(),
              "blocked prompt eval states must not materialize a successful normalized response");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == dasall::contracts::ResultCode::PolicyDenied,
              "blocked prompt eval states should map to PolicyDenied");
  assert_true(result.error_info.has_value(),
              "blocked prompt eval states should surface an ErrorInfo payload");
  assert_true(contains_token(result.diagnostics, "llm_failure:prompt_governance"),
              "blocked prompt eval states should surface prompt governance diagnostics");
  assert_true(contains_token(result.diagnostics, "eval_blocked"),
              "blocked prompt eval states should emit the eval_blocked diagnostic token");
  assert_true(contains_token(result.diagnostics, "error_type:policy"),
              "blocked prompt eval states should project the policy error category");
}

}  // namespace

int main() {
  try {
    test_invoke_stage_maps_retired_prompt_release_to_policy_denied();
    test_invoke_stage_maps_blocked_prompt_eval_to_policy_denied();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
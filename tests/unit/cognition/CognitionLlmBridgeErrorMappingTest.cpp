#include <exception>
#include <iostream>
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
  request.request_id = "req-020-error";
  request.trace_id = "trace-020-error";
  request.stage_name = "execution";
  request.task_type = "action_decision";
  request.messages = {
      "system: produce a structured action decision",
      "user: choose the next governed action",
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

void test_invoke_stage_projects_llm_failure_to_cognition_error_surface() {
  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_result(MockLLMManager::make_failure_result(
      dasall::contracts::ResultCode::ProviderTimeout,
      "provider transport failed",
      dasall::llm::LLMFailureCategory::AdapterTransport,
      "llm.exec.primary",
      std::string{"req-020-error"}));

  CognitionLlmBridge bridge(llm_manager);
  const auto result = bridge.invoke_stage(make_call_request());

  assert_true(!result.response.has_value(),
              "failed llm calls must not materialize a normalized response payload");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == dasall::contracts::ResultCode::ProviderTimeout,
              "adapter transport failures should surface the provider timeout result code");
  assert_true(result.error_info.has_value(),
              "failed llm calls must expose an ErrorInfo payload");
  assert_equal(std::string("execution"), result.error_info->details.stage,
               "bridge should rewrite llm failures onto the canonical cognition stage");
  assert_equal(std::string("cognition.llm_bridge"), result.error_info->source_ref.ref_type,
               "error source should point back to the cognition llm bridge");
  assert_equal(std::string("execution"), result.error_info->source_ref.ref_id,
               "error source id should use the normalized stage key");
  assert_true(contains_token(result.diagnostics, "llm_failure:adapter_transport"),
              "bridge diagnostics should preserve the llm failure category");
}

}  // namespace

int main() {
  try {
    test_invoke_stage_projects_llm_failure_to_cognition_error_surface();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
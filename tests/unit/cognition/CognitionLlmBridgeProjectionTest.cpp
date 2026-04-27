#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "llm/CognitionLlmBridge.h"
#include "MockLLMManager.h"
#include "route/ModelSelectionHint.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::BudgetContext;
using dasall::cognition::ModelCapabilityTier;
using dasall::cognition::StageModelHint;
using dasall::cognition::llm_bridge::CognitionLlmBridge;
using dasall::cognition::llm_bridge::StageLlmCallRequest;
using dasall::cognition::llm_bridge::StageSchemaKind;
using dasall::cognition::llm_bridge::StageSchemaSpec;
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
  request.request_id = "req-020-projection";
  request.trace_id = "trace-020-projection";
  request.stage_name = "planning";
  request.task_type = "plan";
  request.messages = {
      "system: produce a structured plan graph",
      "user: build a cautious multi-step plan for the request",
  };
  request.model_hint = StageModelHint{
      .stage_name = "planning",
      .task_type = "plan",
      .capability_tier = ModelCapabilityTier::Advanced,
      .max_output_tokens = 600U,
      .deadline_ms = 1500U,
      .requires_structured_output = true,
      .requires_reasoning_trace = true,
      .cost_sensitivity = 0.70F,
      .preferred_provider = {},
  };
  request.budget_context = BudgetContext{
      .total_budget_tokens = 2048U,
      .consumed_tokens = 1600U,
      .remaining_tokens = 420U,
      .budget_utilization = 0.78F,
      .context_was_truncated = false,
      .near_budget_limit = true,
  };
  request.schema_spec = StageSchemaSpec{
      .schema_kind = StageSchemaKind::JsonSchema,
      .output_schema_ref = "schema://cognition/plan_graph",
      .allow_plain_text_fallback = false,
  };
  return request;
}

void test_invoke_stage_projects_stage_hint_and_redacts_provider_private_fields() {
  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_result(MockLLMManager::make_success_result(
      R"({"plan_id":"plan-020","reasoning_content":"hidden-chain","summary":"ok"})",
      "llm.plan.primary",
      std::string{"req-020-projection"}));

  CognitionLlmBridge bridge(llm_manager);
  const auto result = bridge.invoke_stage(make_call_request());

  assert_true(result.response.has_value(),
              "projection path should normalize a successful llm response");
  assert_true(!result.result_code.has_value(),
              "successful bridge path must not emit a result code");
  assert_equal(std::string("llm.plan.primary"), result.resolved_route,
               "bridge should preserve the resolved llm route");
  assert_true(contains_token(result.warnings, "budget:near_limit"),
              "near-budget requests should surface a budget warning");
  assert_true(contains_token(result.warnings, "provider_private_redacted:reasoning_content"),
              "provider-private response fields must be redacted in normalized payload");
  assert_true(result.response->content_payload.has_value() &&
                  result.response->content_payload->find("hidden-chain") == std::string::npos,
              "normalized payload must not leak reasoning_content values");
  assert_true(result.response->content_payload.has_value() &&
                  result.response->content_payload->find("[REDACTED]") != std::string::npos,
              "redacted payload should retain an explicit redaction marker");

  assert_equal(1, llm_manager->generate_call_count(),
               "bridge should issue exactly one unary llm request");
  assert_true(llm_manager->last_request().has_value(),
              "mock llm manager should record the projected request");

  const auto& projected_request = *llm_manager->last_request();
  assert_equal(std::string("planning"), projected_request.stage,
               "projected llm stage must use the canonical planning key");
  assert_equal(std::string("plan"), projected_request.task_type,
               "projected llm task type should preserve cognition semantics");
  assert_true(projected_request.request.max_output_tokens.has_value() &&
                  *projected_request.request.max_output_tokens == 420U,
              "budget-aware projection should clamp output tokens to remaining budget");
  assert_true(projected_request.request.timeout_ms.has_value() &&
                  *projected_request.request.timeout_ms == 1500U,
              "projection should carry the stage deadline into llm timeout hint");
  assert_true(projected_request.request.output_schema_ref.has_value() &&
                  *projected_request.request.output_schema_ref == "schema://cognition/plan_graph",
              "structured planning stage should propagate the schema reference");
  assert_true(projected_request.request.response_format.has_value() &&
                  *projected_request.request.response_format == "json_schema",
              "structured planning stage should request json_schema output");
  assert_true(projected_request.request.runtime_budget.has_value() &&
                  projected_request.request.runtime_budget->max_tokens.has_value() &&
                  *projected_request.request.runtime_budget->max_tokens == 2048U,
              "projection should expose the total runtime token budget to llm");
  assert_true(projected_request.selection_hint != nullptr,
              "bridge should emit a model selection hint for llm routing");
  assert_equal(std::string("advanced"), projected_request.selection_hint->complexity_tier,
               "advanced stage hints should map to the advanced complexity lane");
  assert_equal(std::string("interactive"), projected_request.selection_hint->latency_sla_tier,
               "1500ms deadlines should map to the interactive latency lane");
  assert_equal(std::string("tight"), projected_request.selection_hint->budget_tier,
               "near-budget requests should map to the tight budget lane");
  assert_true(projected_request.selection_hint->requires_reasoning,
              "planning path requiring reasoning trace should project the reasoning flag");
  assert_true(projected_request.selection_hint->target_output_tokens == 420U,
              "selection hint target output tokens should match the clamped output budget");
}

}  // namespace

int main() {
  try {
    test_invoke_stage_projects_stage_hint_and_redacts_provider_private_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
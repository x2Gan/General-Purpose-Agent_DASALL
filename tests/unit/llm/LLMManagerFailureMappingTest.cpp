#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/prompt/IPromptPipeline.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"

#include "../../mocks/include/MockLLMAdapter.h"

#include "ModelRouterTestSupport.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::PromptComposeResult;
using dasall::llm::AdapterCallResult;
using dasall::llm::LLMFailureCategory;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::prompt::IPromptPipeline;
using dasall::llm::prompt::PromptPipelineConfig;
using dasall::llm::prompt::PromptPipelineResult;
using dasall::llm::prompt::PromptPolicyDecision;
using dasall::llm::prompt::PromptPolicyDisposition;
using dasall::llm::prompt::PromptPolicyInput;
using dasall::llm::prompt::PromptQuery;
using dasall::llm::route::AdapterRegistration;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class StaticPromptPipeline final : public IPromptPipeline {
 public:
  explicit StaticPromptPipeline(PromptPipelineResult result)
      : result_(std::move(result)) {}

  bool init(const PromptPipelineConfig&) override { return true; }

  PromptPipelineResult run(const PromptQuery&,
                           const dasall::contracts::PromptComposeRequest&,
                           const PromptPolicyInput&) const override {
    return result_;
  }

 private:
  PromptPipelineResult result_;
};

PromptPipelineResult make_policy_deny_result() {
  return PromptPipelineResult{
      .disposition = PromptPolicyDisposition::Deny,
      .compose_result = PromptComposeResult{
          .messages = std::vector<std::string>{"system", "user"},
          .selected_prompt_id = "prompt.planner.deny",
          .selected_version = "2026-04-13.3",
          .estimated_tokens = 32,
          .pruned_sections = std::nullopt,
          .composition_warnings = std::nullopt,
      },
      .policy_decision = PromptPolicyDecision{
          .disposition = PromptPolicyDisposition::Deny,
          .governed_messages = {},
          .redactions = {},
          .tool_visibility_patch = {},
          .reason = "prompt_release_not_allowed",
      },
      .registry_result = dasall::llm::prompt::PromptRegistryResult{
          .code = std::nullopt,
          .release = dasall::contracts::PromptRelease{
              .prompt_id = "prompt.planner.deny",
              .version = "2026-04-13.3",
              .stage = dasall::contracts::CompositionStage::Planning,
              .eval_status = dasall::contracts::PromptEvalStatus::Stable,
              .release_scope = "stable",
              .system_instructions = "system",
              .task_template = "task",
              .output_schema_ref = std::nullopt,
              .few_shot_refs = std::nullopt,
              .policy_notes = std::nullopt,
              .rollback_from = std::nullopt,
              .trusted_source = "profiles",
              .tags = std::nullopt,
          },
          .selected_prompt_id = "prompt.planner.deny",
          .selected_version = "2026-04-13.3",
          .selection_reason = "selected_default_release",
          .trusted_sources_matched = {"profiles"},
      },
      .reason = "prompt_release_not_allowed",
  };
}

PromptPipelineResult make_allow_result() {
  return PromptPipelineResult{
      .disposition = PromptPolicyDisposition::Allow,
      .compose_result = PromptComposeResult{
          .messages = std::vector<std::string>{"system", "user"},
          .selected_prompt_id = "prompt.planner.allow",
          .selected_version = "2026-04-13.4",
          .estimated_tokens = 32,
          .pruned_sections = std::nullopt,
          .composition_warnings = std::nullopt,
      },
      .policy_decision = PromptPolicyDecision{
          .disposition = PromptPolicyDisposition::Allow,
          .governed_messages = {"system", "user"},
          .redactions = {},
          .tool_visibility_patch = {},
          .reason = "allow",
      },
      .registry_result = dasall::llm::prompt::PromptRegistryResult{
          .code = std::nullopt,
          .release = dasall::contracts::PromptRelease{
              .prompt_id = "prompt.planner.allow",
              .version = "2026-04-13.4",
              .stage = dasall::contracts::CompositionStage::Planning,
              .eval_status = dasall::contracts::PromptEvalStatus::Stable,
              .release_scope = "stable",
              .system_instructions = "system",
              .task_template = "task",
              .output_schema_ref = std::nullopt,
              .few_shot_refs = std::nullopt,
              .policy_notes = std::nullopt,
              .rollback_from = std::nullopt,
              .trusted_source = "profiles",
              .tags = std::nullopt,
          },
          .selected_prompt_id = "prompt.planner.allow",
          .selected_version = "2026-04-13.4",
          .selection_reason = "selected_default_release",
          .trusted_sources_matched = {"profiles"},
      },
      .reason = {},
  };
}

AdapterRegistration make_registration(std::string provider_id,
                                      std::string model_id,
                                      std::string adapter_id,
                                      std::string deployment_type,
                                      std::vector<std::string> capability_tags,
                                      std::shared_ptr<MockLLMAdapter> adapter) {
  return AdapterRegistration{
      .provider_id = std::move(provider_id),
      .model_id = std::move(model_id),
      .adapter_id = std::move(adapter_id),
      .deployment_type = std::move(deployment_type),
      .capability_tags = std::move(capability_tags),
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

LLMGenerateRequest make_request() {
  LLMRequest request;
  request.request_id = "req-024-failure";
  request.llm_call_id = "call-024-failure";
  request.model_route = "cloud.reasoning";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"failure mapping"};
  request.created_at = 1712966402000LL;
  request.max_output_tokens = 128U;

  return LLMGenerateRequest{
      .stage = "planner",
      .task_type = "plan",
      .request = std::move(request),
      .selection_hint = std::make_shared<const dasall::llm::ModelSelectionHint>(
          dasall::llm::ModelSelectionHint{
              .stage = "planner",
              .task_type = "plan",
              .complexity_tier = "standard",
              .latency_sla_tier = "interactive",
              .budget_tier = "hard_cap",
              .requires_tools = true,
              .requires_reasoning = false,
              .prefers_visible_reasoning = false,
              .estimated_input_tokens = 256U,
              .target_output_tokens = 128U,
              .previous_route_failures = 0U,
          }),
  };
}

AdapterCallResult make_provider_failure(std::string route_key) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::ResultCodeCategory::Provider;
  error.retryable = false;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout);
  error.details.message = "provider call failed on " + route_key;
  error.details.stage = "mock.adapter.generate";
  error.source_ref.ref_type = "route";
  error.source_ref.ref_id = std::move(route_key);

  AdapterCallResult result;
  result.error = std::move(error);
  result.result_code = dasall::contracts::ResultCode::ProviderTimeout;
  return result;
}

void test_prompt_governance_failure_stops_before_adapter_dispatch() {
  auto pipeline = std::make_shared<StaticPromptPipeline>(make_policy_deny_result());
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto catalog_snapshot =
      std::make_shared<const dasall::llm::provider::ProviderCatalogSnapshot>(
          dasall::llm::test_support::make_default_catalog());

  assert_true(registry->init(dasall::llm::route::AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "AdapterRegistry should initialize for governance failure coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  assert_true(registry->register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-chat",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud", "external"},
                                                          adapter)),
              "AdapterRegistry should register the route even though governance failure should stop before dispatch");

  LLMManager manager(pipeline, router, registry, executor, normalizer, aggregator,
                     catalog_snapshot);
  assert_true(manager.init(dasall::llm::test_support::make_config(
                  "planner", "cloud.reasoning", std::string("lan.general"),
                  {"local.small"}, false, false)),
              "LLMManager should initialize for governance failure coverage");

  const auto result = manager.generate(make_request());

  assert_true(result.has_consistent_values() && !result.response.has_value() &&
                  result.failure_category.has_value() &&
                  *result.failure_category == LLMFailureCategory::PromptGovernance,
              "LLMManager should map PromptPipeline deny to PromptGovernance without producing a response");
  assert_true(result.code.has_value() &&
                  *result.code == dasall::contracts::ResultCode::PolicyDenied,
              "LLMManager should surface governance failures through PolicyDenied");
  assert_true(result.attempted_routes.empty(),
              "LLMManager should not attempt any route when PromptPipeline denies the request");
  assert_equal(0, adapter->generate_call_count(),
               "LLMManager should not call any adapter when governance fails before routing");
}

void test_multiple_route_failures_collapse_into_fallback_exhausted() {
  auto pipeline = std::make_shared<StaticPromptPipeline>(make_allow_result());
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto catalog_snapshot =
      std::make_shared<const dasall::llm::provider::ProviderCatalogSnapshot>(
          dasall::llm::test_support::make_default_catalog());

  assert_true(registry->init(dasall::llm::route::AdapterRegistryConfig{.blocked_failure_threshold = 4U}),
              "AdapterRegistry should initialize for fallback exhausted coverage");

  auto cloud_adapter = std::make_shared<MockLLMAdapter>();
  cloud_adapter->set_generate_result(make_provider_failure("deepseek-prod/deepseek-chat"));
  auto lan_adapter = std::make_shared<MockLLMAdapter>();
  lan_adapter->set_generate_result(make_provider_failure("lan-ollama/lan-general"));

  assert_true(registry->register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-chat",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud", "external"},
                                                          cloud_adapter)),
              "AdapterRegistry should register the primary route for fallback exhausted coverage");
  assert_true(registry->register_adapter(make_registration("lan-ollama",
                                                          "lan-general",
                                                          "ollama-lan",
                                                          "lan",
                                                          {"lan", "internal"},
                                                          lan_adapter)),
              "AdapterRegistry should register the fallback route for fallback exhausted coverage");

  auto config = dasall::llm::test_support::make_config(
      "planner", "cloud.reasoning", std::string("lan.general"),
      {"local.small"}, false, false);
  config.timeout_policy.retry_budget = 0U;

  LLMManager manager(pipeline, router, registry, executor, normalizer, aggregator,
                     catalog_snapshot);
  assert_true(manager.init(config),
              "LLMManager should initialize for fallback exhausted coverage");

  const auto result = manager.generate(make_request());

  assert_true(result.has_consistent_values() && !result.response.has_value() &&
                  result.failure_category.has_value() &&
                  *result.failure_category == LLMFailureCategory::FallbackExhausted,
              "LLMManager should elevate multi-route transport failures to FallbackExhausted");
  assert_equal(2, static_cast<int>(result.attempted_routes.size()),
               "LLMManager should keep the full attempt trace when fallback routes are exhausted");
  assert_equal(std::string("lan-ollama/lan-general"), result.resolved_route,
               "LLMManager should expose the last attempted route on fallback exhausted failure");
  assert_true(result.code.has_value() &&
                  *result.code == dasall::contracts::ResultCode::ProviderTimeout,
              "LLMManager should preserve the last route failure code when fallback is exhausted");
}

}  // namespace

int main() {
  try {
    test_prompt_governance_failure_stops_before_adapter_dispatch();
    test_multiple_route_failures_collapse_into_fallback_exhausted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
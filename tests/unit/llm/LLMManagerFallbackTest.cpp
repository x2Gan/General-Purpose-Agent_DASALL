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
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::contracts::PromptComposeResult;
using dasall::llm::AdapterCallResult;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMManagerResult;
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

class AllowPromptPipeline final : public IPromptPipeline {
 public:
  bool init(const PromptPipelineConfig&) override { return true; }

  PromptPipelineResult run(const PromptQuery&,
                           const dasall::contracts::PromptComposeRequest&,
                           const PromptPolicyInput&) const override {
    return PromptPipelineResult{
        .disposition = PromptPolicyDisposition::Allow,
        .compose_result = PromptComposeResult{
            .messages = std::vector<std::string>{
                "system: use fallback if needed",
                "user: route this request",
            },
            .selected_prompt_id = "prompt.planner.fallback",
            .selected_version = "2026-04-13.2",
            .estimated_tokens = 64,
            .pruned_sections = std::nullopt,
            .composition_warnings = std::nullopt,
        },
        .policy_decision = PromptPolicyDecision{
            .disposition = PromptPolicyDisposition::Allow,
            .governed_messages = {
                "system: use fallback if needed",
                "user: route this request",
            },
            .redactions = {},
            .tool_visibility_patch = {},
            .reason = "allow",
        },
        .registry_result = dasall::llm::prompt::PromptRegistryResult{
            .code = std::nullopt,
            .release = dasall::contracts::PromptRelease{
                .prompt_id = "prompt.planner.fallback",
                .version = "2026-04-13.2",
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
            .selected_prompt_id = "prompt.planner.fallback",
            .selected_version = "2026-04-13.2",
            .selection_reason = "selected_default_release",
            .trusted_sources_matched = {"profiles"},
        },
        .reason = {},
    };
  }
};

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
  request.request_id = "req-024-fallback";
  request.llm_call_id = "call-024-fallback";
  request.model_route = "cloud.reasoning";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"try cloud first"};
  request.created_at = 1712966401000LL;
  request.max_output_tokens = 256U;

  return LLMGenerateRequest{
      .stage = "planner",
      .task_type = "plan",
      .request = std::move(request),
      .prompt_release_id_override = std::nullopt,
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
              .estimated_input_tokens = 512U,
              .target_output_tokens = 256U,
              .previous_route_failures = 0U,
          }),
  };
}

bool has_attempted_route(const LLMManagerResult& result, const std::string& route_key) {
  return std::find(result.attempted_routes.begin(), result.attempted_routes.end(), route_key) !=
         result.attempted_routes.end();
}

void test_llm_manager_uses_fallback_route_after_primary_transport_failure() {
  auto pipeline = std::make_shared<AllowPromptPipeline>();
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto catalog_snapshot =
      std::make_shared<const dasall::llm::provider::ProviderCatalogSnapshot>(
          dasall::llm::test_support::make_default_catalog());

  assert_true(registry->init(dasall::llm::route::AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "AdapterRegistry should initialize for manager fallback coverage");

  auto cloud_adapter = std::make_shared<MockLLMAdapter>();
  cloud_adapter->set_generate_handler([](const LLMRequest&) {
    dasall::contracts::ErrorInfo error;
    error.failure_type = dasall::contracts::ResultCodeCategory::Provider;
    error.retryable = false;
    error.safe_to_replan = false;
    error.details.code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout);
    error.details.message = "cloud provider failed";
    error.details.stage = "mock.adapter.generate";
    error.source_ref.ref_type = "route";
    error.source_ref.ref_id = "deepseek-prod/deepseek-chat";

    AdapterCallResult result;
    result.error = std::move(error);
    result.result_code = dasall::contracts::ResultCode::ProviderTimeout;
    return result;
  });

  auto lan_adapter = std::make_shared<MockLLMAdapter>();
  lan_adapter->set_generate_handler([](const LLMRequest& request) {
    LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = LLMResponseKind::DirectResponse;
    response.content_payload = "fallback-success";
    response.finish_reason = "stop";

    AdapterCallResult result;
    result.response = std::move(response);
    return result;
  });

  assert_true(registry->register_adapter(make_registration("deepseek-prod",
                                                          "deepseek-chat",
                                                          "deepseek-cloud",
                                                          "cloud",
                                                          {"cloud", "external"},
                                                          cloud_adapter)),
              "AdapterRegistry should register the primary cloud route for fallback coverage");
  assert_true(registry->register_adapter(make_registration("lan-ollama",
                                                          "lan-general",
                                                          "ollama-lan",
                                                          "lan",
                                                          {"lan", "internal"},
                                                          lan_adapter)),
              "AdapterRegistry should register the LAN fallback route for fallback coverage");

  auto config = dasall::llm::test_support::make_config(
      "planner", "cloud.reasoning", std::string("lan.general"),
      {"local.small"}, false, false);
  config.timeout_policy.retry_budget = 0U;

  LLMManager manager(pipeline, router, registry, executor, normalizer, aggregator,
                     catalog_snapshot);
  assert_true(manager.init(config),
              "LLMManager should initialize for fallback coverage");

  const auto result = manager.generate(make_request());

  assert_true(result.has_consistent_values() && result.response.has_value(),
              "LLMManager should still return a success result when a later fallback route succeeds");
  assert_equal(std::string("lan-ollama/lan-general"), result.resolved_route,
               "LLMManager should expose the fallback route that ultimately succeeded");
  assert_equal(2, static_cast<int>(result.attempted_routes.size()),
               "LLMManager should keep the full attempted route list across fallback");
  assert_true(result.fallback_used,
              "LLMManager should mark fallback_used when a non-primary route succeeds");
  assert_true(has_attempted_route(result, "deepseek-prod/deepseek-chat") &&
                  has_attempted_route(result, "lan-ollama/lan-general"),
              "LLMManager should retain both primary and fallback routes in attempted_routes");
  assert_equal(1, cloud_adapter->generate_call_count(),
               "LLMManager should only spend one attempt on the primary route when retry_budget is zero");
  assert_equal(1, lan_adapter->generate_call_count(),
               "LLMManager should invoke the fallback adapter after the primary route fails");
}

}  // namespace

int main() {
  try {
    test_llm_manager_uses_fallback_route_after_primary_transport_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
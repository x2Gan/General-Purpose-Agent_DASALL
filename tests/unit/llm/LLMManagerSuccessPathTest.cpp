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
using dasall::llm::AdapterUsageFragment;
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
  bool init(const PromptPipelineConfig&) override {
    initialized_ = true;
    return true;
  }

  PromptPipelineResult run(const PromptQuery& query,
                           const dasall::contracts::PromptComposeRequest& compose_request,
                           const PromptPolicyInput& policy_input) const override {
    last_query_ = query;
    last_compose_request_ = compose_request;
    last_policy_input_ = policy_input;
    return PromptPipelineResult{
        .disposition = PromptPolicyDisposition::Allow,
        .compose_result = PromptComposeResult{
            .messages = std::vector<std::string>{
                "system: produce a plan",
                "user: explain the unary success path",
            },
            .selected_prompt_id = "prompt.planner.default",
            .selected_version = "2026-04-13.1",
            .estimated_tokens = 64,
            .pruned_sections = std::nullopt,
            .composition_warnings = std::nullopt,
        },
        .policy_decision = PromptPolicyDecision{
            .disposition = PromptPolicyDisposition::Allow,
            .governed_messages = {
                "system: produce a plan",
                "user: explain the unary success path",
            },
            .redactions = {},
            .tool_visibility_patch = {"builtin:all"},
            .reason = "allow",
        },
        .registry_result = dasall::llm::prompt::PromptRegistryResult{
            .code = std::nullopt,
            .release = dasall::contracts::PromptRelease{
                .prompt_id = "prompt.planner.default",
                .version = "2026-04-13.1",
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
            .selected_prompt_id = "prompt.planner.default",
            .selected_version = "2026-04-13.1",
            .selection_reason = "selected_default_release",
            .trusted_sources_matched = {"profiles"},
        },
        .reason = {},
    };
  }

  [[nodiscard]] bool initialized() const { return initialized_; }
  [[nodiscard]] std::optional<PromptQuery> last_query() const { return last_query_; }
  [[nodiscard]] std::optional<dasall::contracts::PromptComposeRequest> last_compose_request() const {
    return last_compose_request_;
  }

 private:
  bool initialized_ = false;
  mutable std::optional<PromptQuery> last_query_;
  mutable std::optional<dasall::contracts::PromptComposeRequest> last_compose_request_;
  mutable std::optional<PromptPolicyInput> last_policy_input_;
};

AdapterRegistration make_registration(
    std::shared_ptr<MockLLMAdapter> adapter) {
  return AdapterRegistration{
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-chat",
      .adapter_id = "deepseek-cloud",
      .deployment_type = "cloud",
      .capability_tags = {"cloud", "external", "unary"},
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

LLMGenerateRequest make_request() {
  LLMRequest request;
  request.request_id = "req-024-success";
  request.llm_call_id = "call-024-success";
  request.model_route = "cloud.reasoning";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"draft a success path"};
  request.created_at = 1712966400000LL;
  request.output_schema_ref = "schema://planner";
  request.response_format = "json_object";
  request.max_output_tokens = 512U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"unit", "llm-manager-success"};

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
              .estimated_input_tokens = 1024U,
              .target_output_tokens = 512U,
              .previous_route_failures = 0U,
          }),
  };
}

bool has_tag(const LLMManagerResult& result, const std::string& expected_tag) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find(result.response->tags->begin(), result.response->tags->end(), expected_tag) !=
             result.response->tags->end();
}

void test_llm_manager_generates_success_result_and_usage_tags() {
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
              "AdapterRegistry should initialize for manager success coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler([](const LLMRequest& request) {
    LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = LLMResponseKind::DirectResponse;
    response.content_payload = "manager-success";
    response.finish_reason = "stop";

    AdapterCallResult result;
    result.response = std::move(response);
    result.usage = AdapterUsageFragment{
        .prompt_tokens = 120U,
        .completion_tokens = 30U,
        .total_tokens = 150U,
        .prompt_cache_hit_tokens = std::nullopt,
        .prompt_cache_miss_tokens = std::nullopt,
    };
    result.provider_diagnostics.provider_trace_id = "trace-024-success";
    result.provider_diagnostics.audit_tags = {"normalized"};
    return result;
  });
  assert_true(registry->register_adapter(make_registration(adapter)),
              "AdapterRegistry should register the manager success route");

  LLMManager manager(pipeline, router, registry, executor, normalizer, aggregator,
                     catalog_snapshot);
  assert_true(manager.init(dasall::llm::test_support::make_config(
            "planner", "cloud.reasoning", std::nullopt,
            {"local.small"}, false, false)),
              "LLMManager should initialize with injected unary dependencies");

  const auto result = manager.generate(make_request());

  assert_true(result.has_consistent_values() && result.response.has_value(),
              "LLMManager should return a consistent success result on the unary happy path");
  assert_equal(std::string("deepseek-prod/deepseek-chat"), result.resolved_route,
               "LLMManager should expose the concrete resolved route on success");
  assert_equal(1, static_cast<int>(result.attempted_routes.size()),
               "LLMManager should record exactly one attempted route on the primary success path");
  assert_true(!result.fallback_used,
              "LLMManager should keep fallback_used false when the primary route succeeds");
  assert_true(!result.failure_category.has_value(),
              "LLMManager should not attach a failure category on the success path");
  assert_true(result.response->model_name.has_value() &&
                  *result.response->model_name == "deepseek-chat",
              "LLMManager should let ResponseNormalizer backfill model_name from provider metadata");
  assert_true(result.response->prompt_id.has_value() &&
                  *result.response->prompt_id == "prompt.planner.default",
              "LLMManager should stamp the selected prompt_id onto the normalized response");
  assert_true(result.response->prompt_version.has_value() &&
                  *result.response->prompt_version == "2026-04-13.1",
              "LLMManager should stamp the selected prompt_version onto the normalized response");
  assert_true(has_tag(result, "route=deepseek-prod/deepseek-chat"),
              "LLMManager should preserve the resolved route as a response tag for later observability sinks");
  assert_true(has_tag(result, "usage:prompt_tokens=120"),
              "LLMManager should append aggregated prompt token anchors onto the response tags");
  assert_true(has_tag(result, "usage:estimated_cost_usd=0.000042"),
              "LLMManager should append the aggregated estimated cost onto the response tags");
  assert_true(has_tag(result, "provider_trace_id=trace-024-success"),
              "LLMManager should preserve provider trace identifiers emitted by ResponseNormalizer");
  assert_true(adapter->last_request().has_value() &&
                  adapter->last_request()->model_route.has_value() &&
                  *adapter->last_request()->model_route == "deepseek-prod/deepseek-chat",
              "LLMManager should pass the concrete route selected by ModelRouter into the adapter request");
  assert_true(adapter->last_request()->prompt_id.has_value() &&
                  *adapter->last_request()->prompt_id == "prompt.planner.default",
              "LLMManager should forward prompt_id from PromptPipeline into the adapter request");
  assert_true(pipeline->initialized() && pipeline->last_query().has_value(),
              "LLMManager should initialize and invoke PromptPipeline before provider dispatch");
}

}  // namespace

int main() {
  try {
    test_llm_manager_generates_success_result_and_usage_tags();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
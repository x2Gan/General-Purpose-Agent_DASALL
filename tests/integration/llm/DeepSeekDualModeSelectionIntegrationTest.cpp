#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/observability/LLMMetricsBridge.h"
#include "../../../llm/src/observability/LLMTraceBridge.h"
#include "../../../llm/src/prompt/PromptPipeline.h"

#include "../../mocks/include/MockLLMAdapter.h"
#include "../../unit/llm/ModelRouterTestSupport.h"

#include "LLMIntegrationTestSupport.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMManagerResult;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ModelSelectionHint;
using dasall::llm::observability::LLMMetricsBridge;
using dasall::llm::observability::LLMTraceBridge;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::provider::ProviderCatalogSnapshot;
using dasall::tests::integration::llm_support::RecordingLogger;
using dasall::tests::integration::llm_support::RecordingMeter;
using dasall::tests::integration::llm_support::RecordingMetricsProvider;
using dasall::tests::integration::llm_support::RecordingTracer;
using dasall::tests::integration::llm_support::find_log_attr;
using dasall::tests::integration::llm_support::has_result_tag;
using dasall::tests::integration::llm_support::kPromptAssetRoot;
using dasall::tests::integration::llm_support::make_registration;
using dasall::tests::integration::llm_support::trace_attr_as_string;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

std::string describe_result(const LLMManagerResult& result) {
  std::ostringstream builder;
  builder << "resolved_route=" << result.resolved_route
          << ", attempted_routes=" << result.attempted_routes.size()
          << ", fallback_used=" << (result.fallback_used ? "true" : "false");
  if (result.code.has_value()) {
    builder << ", code=" << static_cast<int>(*result.code);
  }
  if (result.failure_category.has_value()) {
    builder << ", failure_category="
            << static_cast<int>(*result.failure_category);
  }
  if (result.error.has_value()) {
    builder << ", error_message=" << result.error->details.message
            << ", error_stage=" << result.error->details.stage;
  }
  return builder.str();
}

ProviderCatalogSnapshot make_dual_mode_catalog() {
  auto catalog = dasall::llm::test_support::make_default_catalog();
  for (auto& model : catalog.models) {
    if (model.summary.provider_id == "deepseek-prod" &&
        model.summary.model_id == "deepseek-reasoner") {
      model.verification_states["tools"] = "verified";
      model.summary.verification_state =
          dasall::llm::test_support::aggregate_verification_state(model.verification_states);
    }
  }

  return catalog;
}

LLMSubsystemConfig make_config() {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.reasoning", std::nullopt, {"local.small"}, false, false);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = std::string(kPromptAssetRoot);
  config.prompt_selector_overlay.active_scene = "general";
  config.prompt_selector_overlay.active_persona = "planner";
  return config;
}

LLMGenerateRequest make_request(std::string request_id,
                                std::string llm_call_id,
                                std::string task_type,
                                std::string complexity_tier,
                                std::string latency_sla_tier,
                                std::string budget_tier,
                                bool requires_tools,
                                bool requires_reasoning,
                                bool prefers_visible_reasoning,
                                std::uint32_t estimated_input_tokens,
                                std::uint32_t target_output_tokens) {
  const std::string resolved_task_type = task_type;
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"validate deepseek dual mode routing"};
  request.created_at = 1712966402000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = target_output_tokens;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = estimated_input_tokens + target_output_tokens + 2048U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "dual-mode"};

  return LLMGenerateRequest{
      .stage = "planning",
      .task_type = std::move(task_type),
      .request = std::move(request),
      .selection_hint = std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
          .stage = "planning",
        .task_type = resolved_task_type,
          .complexity_tier = std::move(complexity_tier),
          .latency_sla_tier = std::move(latency_sla_tier),
          .budget_tier = std::move(budget_tier),
          .requires_tools = requires_tools,
          .requires_reasoning = requires_reasoning,
          .prefers_visible_reasoning = prefers_visible_reasoning,
          .estimated_input_tokens = estimated_input_tokens,
          .target_output_tokens = target_output_tokens,
          .previous_route_failures = 0U,
      }),
  };
}

AdapterCallResult make_adapter_result(const LLMRequest& request,
                                      std::string content_payload,
                                      std::string provider_trace_id,
                                      std::optional<std::string> reasoning_content,
                                      std::uint32_t prompt_tokens,
                                      std::uint32_t completion_tokens,
                                      std::uint32_t cache_hit_tokens,
                                      std::uint32_t cache_miss_tokens) {
  LLMResponse response;
  response.request_id = request.request_id;
  response.llm_call_id = request.llm_call_id;
  response.response_kind = LLMResponseKind::DirectResponse;
  response.content_payload = std::move(content_payload);
  response.finish_reason = "stop";

  AdapterCallResult result;
  result.response = std::move(response);
  result.usage = AdapterUsageFragment{
      .prompt_tokens = prompt_tokens,
      .completion_tokens = completion_tokens,
      .total_tokens = prompt_tokens + completion_tokens,
      .prompt_cache_hit_tokens = cache_hit_tokens,
      .prompt_cache_miss_tokens = cache_miss_tokens,
  };
  result.provider_diagnostics.provider_trace_id = std::move(provider_trace_id);
  if (reasoning_content.has_value()) {
    result.provider_diagnostics.reasoning_content = std::move(*reasoning_content);
    result.provider_diagnostics.audit_tags = {"dual_mode_reasoning_strip"};
  }
  return result;
}

struct DualModeRunArtifacts {
  LLMManagerResult result;
  std::shared_ptr<RecordingLogger> logger;
  std::shared_ptr<RecordingMeter> meter;
  std::shared_ptr<RecordingMetricsProvider> metrics_provider;
  std::shared_ptr<RecordingTracer> tracer;
  std::shared_ptr<MockLLMAdapter> chat_adapter;
  std::shared_ptr<MockLLMAdapter> reasoner_adapter;
};

DualModeRunArtifacts run_dual_mode_case(const LLMGenerateRequest& request) {
  auto logger = std::make_shared<RecordingLogger>();
  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto tracer = std::make_shared<RecordingTracer>();
  auto metrics_bridge = std::make_shared<LLMMetricsBridge>(logger, metrics_provider);
  auto trace_bridge = std::make_shared<LLMTraceBridge>(tracer);

  auto prompt_pipeline = std::make_shared<PromptPipeline>();
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto catalog_snapshot =
      std::make_shared<const ProviderCatalogSnapshot>(make_dual_mode_catalog());

  assert_true(
      registry->init(dasall::llm::route::AdapterRegistryConfig{
          .blocked_failure_threshold = 6U,
      }),
      "DeepSeek dual-mode integration should initialize AdapterRegistry before manager wiring");

  auto chat_adapter = std::make_shared<MockLLMAdapter>();
  chat_adapter->set_generate_handler([](const LLMRequest& adapter_request) {
    return make_adapter_result(adapter_request,
                               "chat-mode-success",
                               "trace-030-chat",
                               std::nullopt,
                               64U,
                               24U,
                               16U,
                               48U);
  });

  auto reasoner_adapter = std::make_shared<MockLLMAdapter>();
  reasoner_adapter->set_generate_handler([](const LLMRequest& adapter_request) {
    return make_adapter_result(adapter_request,
                               "reasoner-mode-success",
                               "trace-030-reasoner",
                               std::string("provider-private reasoning"),
                               96U,
                               48U,
                               24U,
                               72U);
  });

  assert_true(registry->register_adapter(make_registration(
                  "deepseek-prod",
                  "deepseek-chat",
                  "deepseek-chat-cloud",
                  "cloud",
                  {"cloud", "external", "deepseek"},
                  chat_adapter)),
              "DeepSeek dual-mode integration should register the chat route");
  assert_true(registry->register_adapter(make_registration(
                  "deepseek-prod",
                  "deepseek-reasoner",
                  "deepseek-reasoner-cloud",
                  "cloud",
                  {"cloud", "external", "deepseek"},
                  reasoner_adapter)),
              "DeepSeek dual-mode integration should register the reasoner route");

  LLMManager manager(prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
                     catalog_snapshot,
                     metrics_bridge,
                     trace_bridge);
  assert_true(manager.init(make_config()),
              "DeepSeek dual-mode integration should initialize LLMManager with real PromptPipeline and observability bridges");

  return DualModeRunArtifacts{
      .result = manager.generate(request),
      .logger = logger,
      .meter = meter,
      .metrics_provider = metrics_provider,
      .tracer = tracer,
      .chat_adapter = chat_adapter,
      .reasoner_adapter = reasoner_adapter,
  };
}

void test_dual_mode_selects_reasoner_for_reasoning_workload() {
  const auto artifacts = run_dual_mode_case(make_request(
      "req-030-reasoner",
      "call-030-reasoner",
      "plan",
      "high",
      "balanced",
      "balanced",
      false,
      true,
      true,
      8192U,
      4096U));

  if (!(artifacts.result.has_consistent_values() && artifacts.result.response.has_value())) {
    throw std::runtime_error(
        "DeepSeek reasoner workload did not succeed: " + describe_result(artifacts.result));
  }

  assert_equal(std::string("deepseek-prod/deepseek-reasoner"),
               artifacts.result.resolved_route,
               "DeepSeek dual-mode integration should upgrade to the reasoner model for explicit reasoning workloads");
  assert_equal(0, artifacts.chat_adapter->generate_call_count(),
               "DeepSeek dual-mode integration should not invoke the chat route when reasoner is selected");
  assert_equal(1, artifacts.reasoner_adapter->generate_call_count(),
               "DeepSeek dual-mode integration should invoke the reasoner route exactly once");
  assert_true(artifacts.result.response->model_name.has_value() &&
                  *artifacts.result.response->model_name == "deepseek-reasoner",
              "DeepSeek dual-mode integration should preserve the selected reasoner model name on the normalized response");
  assert_true(has_result_tag(artifacts.result, "route=deepseek-prod/deepseek-reasoner") &&
                  has_result_tag(artifacts.result, "selection_reason=selected_primary_route") &&
                  has_result_tag(artifacts.result, "selection_reason=requires_reasoning") &&
                  has_result_tag(artifacts.result, "selection_reason=visible_reasoning_preferred") &&
                  has_result_tag(artifacts.result, "audit=reasoning_content_stripped") &&
                  has_result_tag(artifacts.result, "audit=provider_audit:dual_mode_reasoning_strip") &&
                  has_result_tag(artifacts.result, "reasoning_content_stripped=true"),
              "DeepSeek dual-mode integration should expose reasoner upgrade reasons and reasoning strip facts through provider-neutral response tags");

  assert_true(artifacts.logger->events.size() == 1U,
              "DeepSeek dual-mode integration should emit one structured log entry for the reasoner path");
  const auto& log_event = artifacts.logger->events.front();
  assert_true(find_log_attr(log_event, "resolved_route") != nullptr &&
                  *find_log_attr(log_event, "resolved_route") ==
                      "deepseek-prod/deepseek-reasoner" &&
                  *find_log_attr(log_event, "reasoning_mode_requested") == "thinking" &&
                  *find_log_attr(log_event, "reasoning_mode_effective") == "thinking" &&
                  find_log_attr(log_event, "selection_reason_codes") != nullptr &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("requires_reasoning") != std::string::npos &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("visible_reasoning_preferred") != std::string::npos,
              "DeepSeek dual-mode integration should keep reasoner route selection reasons and reasoning mode in structured logging");
  assert_equal(std::string("llm.observability"),
               artifacts.metrics_provider->last_scope.name,
               "DeepSeek dual-mode integration should request the frozen llm observability meter scope");
  assert_equal(3, static_cast<int>(artifacts.tracer->started_spans.size()),
               "DeepSeek dual-mode integration should emit the three frozen llm trace spans for the reasoner path");
  assert_true(trace_attr_as_string(artifacts.tracer->started_spans.at(0).descriptor.attrs,
                                   "selection_reason_codes")
                      .value_or(std::string())
                      .find("requires_reasoning") != std::string::npos &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "reasoning_mode_requested") ==
                      std::optional<std::string>("thinking") &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "reasoning_mode_effective") ==
                      std::optional<std::string>("thinking"),
              "DeepSeek dual-mode integration should retain reasoner selection reasons and reasoning mode on trace attributes");
}

void test_dual_mode_downgrades_to_chat_for_interactive_hard_cap_workload() {
  const auto artifacts = run_dual_mode_case(make_request(
      "req-030-chat",
      "call-030-chat",
      "plan",
      "standard",
      "interactive",
      "hard_cap",
      true,
      false,
      false,
      2048U,
      1024U));

  if (!(artifacts.result.has_consistent_values() && artifacts.result.response.has_value())) {
    throw std::runtime_error(
        "DeepSeek chat workload did not succeed: " + describe_result(artifacts.result));
  }

  assert_equal(std::string("deepseek-prod/deepseek-chat"),
               artifacts.result.resolved_route,
               "DeepSeek dual-mode integration should downgrade to the chat model when reasoning is optional but latency, budget and tool constraints dominate");
  assert_equal(1, artifacts.chat_adapter->generate_call_count(),
               "DeepSeek dual-mode integration should invoke the chat route exactly once for the downgraded workload");
  assert_equal(0, artifacts.reasoner_adapter->generate_call_count(),
               "DeepSeek dual-mode integration should not invoke the reasoner route after the downgrade decision is made");
  assert_true(artifacts.result.response->model_name.has_value() &&
                  *artifacts.result.response->model_name == "deepseek-chat",
              "DeepSeek dual-mode integration should preserve the selected chat model name on the normalized response");
  assert_true(has_result_tag(artifacts.result, "route=deepseek-prod/deepseek-chat") &&
                  has_result_tag(artifacts.result, "selection_reason=tier_degraded") &&
                  has_result_tag(artifacts.result, "selection_reason=interactive_latency_bias") &&
                  has_result_tag(artifacts.result, "selection_reason=budget_low_cost") &&
                  has_result_tag(artifacts.result, "selection_reason=interactive_hard_cap_downgrade") &&
                  has_result_tag(artifacts.result, "selection_reason=requires_tools") &&
                  !has_result_tag(artifacts.result, "audit=reasoning_content_stripped"),
              "DeepSeek dual-mode integration should expose downgrade reasons while keeping chat responses free of reasoning strip audit facts");

  assert_true(artifacts.logger->events.size() == 1U,
              "DeepSeek dual-mode integration should emit one structured log entry for the chat path");
  const auto& log_event = artifacts.logger->events.front();
  assert_true(find_log_attr(log_event, "resolved_route") != nullptr &&
                  *find_log_attr(log_event, "resolved_route") ==
                      "deepseek-prod/deepseek-chat" &&
                  *find_log_attr(log_event, "reasoning_mode_requested") == "chat" &&
                  *find_log_attr(log_event, "reasoning_mode_effective") == "non_thinking" &&
                  find_log_attr(log_event, "selection_reason_codes") != nullptr &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("tier_degraded") != std::string::npos &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("interactive_latency_bias") != std::string::npos &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("budget_low_cost") != std::string::npos,
              "DeepSeek dual-mode integration should keep chat downgrade reason codes and reasoning mode in structured logging");
  assert_equal(3, static_cast<int>(artifacts.tracer->started_spans.size()),
               "DeepSeek dual-mode integration should emit the three frozen llm trace spans for the chat path");
  assert_true(trace_attr_as_string(artifacts.tracer->started_spans.at(0).descriptor.attrs,
                                   "selection_reason_codes")
                      .value_or(std::string())
                      .find("tier_degraded") != std::string::npos &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "reasoning_mode_requested") ==
                      std::optional<std::string>("chat") &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "reasoning_mode_effective") ==
                      std::optional<std::string>("non_thinking"),
              "DeepSeek dual-mode integration should retain downgrade reason codes and effective chat mode on trace attributes");
}

}  // namespace

int main() {
  try {
    test_dual_mode_selects_reasoner_for_reasoning_workload();
    test_dual_mode_downgrades_to_chat_for_interactive_hard_cap_workload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
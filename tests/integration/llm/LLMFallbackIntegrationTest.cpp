#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../infra/include/logging/ILogger.h"

#include "../../../llm/include/LLMManagerResult.h"
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
using dasall::contracts::ResultCode;
using dasall::infra::logging::LogLevel;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::LLMFailureCategory;
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
using dasall::tests::integration::llm_support::find_sample;
using dasall::tests::integration::llm_support::has_result_tag;
using dasall::tests::integration::llm_support::kPromptAssetRoot;
using dasall::tests::integration::llm_support::make_registration;
using dasall::tests::integration::llm_support::trace_attr_as_string;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kCloudRoute = "deepseek-prod/deepseek-chat";
constexpr std::string_view kLanRoute = "lan-ollama/lan-general";
constexpr std::string_view kLocalRoute = "local-runtime/local-small";

std::string describe_result(const LLMManagerResult& result) {
  std::ostringstream builder;
  builder << "resolved_route=" << result.resolved_route
          << ", attempted_routes="
          << dasall::llm::test_support::join_routes(result.attempted_routes)
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

ProviderCatalogSnapshot make_fallback_catalog() {
  auto catalog = dasall::llm::test_support::make_default_catalog();
  catalog.models.erase(
      std::remove_if(catalog.models.begin(),
                     catalog.models.end(),
                     [](const auto& model) {
                       return model.summary.provider_id == "deepseek-prod" &&
                              model.summary.model_id == "deepseek-reasoner";
                     }),
      catalog.models.end());
  return catalog;
}

LLMSubsystemConfig make_config() {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.reasoning", std::string("lan.general"),
      {"local.small"}, false, true);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = std::string(kPromptAssetRoot);
  config.prompt_selector_overlay.active_scene = "general";
  config.prompt_selector_overlay.active_persona = "planner";
  config.timeout_policy.retry_budget = 0U;
  return config;
}

LLMGenerateRequest make_request(std::string request_id, std::string llm_call_id) {
  LLMRequest request;
  request.request_id = std::move(request_id);
  request.llm_call_id = std::move(llm_call_id);
  request.model_route = "cloud.reasoning";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{
      "validate fallback routing across cloud lan local"};
  request.created_at = 1712966403000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = 1024U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"integration", "fallback"};

  return LLMGenerateRequest{
      .stage = "planning",
      .task_type = "plan",
      .request = std::move(request),
      .prompt_release_id_override = std::nullopt,
      .selection_hint = std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
          .stage = "planning",
          .task_type = "plan",
          .complexity_tier = "standard",
          .latency_sla_tier = "interactive",
          .budget_tier = "hard_cap",
          .requires_tools = false,
          .requires_reasoning = false,
          .prefers_visible_reasoning = false,
          .estimated_input_tokens = 2048U,
          .target_output_tokens = 1024U,
          .previous_route_failures = 0U,
      }),
  };
}

AdapterCallResult make_success_result(const LLMRequest& request,
                                      std::string content_payload,
                                      std::string provider_trace_id,
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
  return result;
}

AdapterCallResult make_transport_failure(std::string route_key,
                                         std::string message) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::ResultCodeCategory::Provider;
  error.retryable = false;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(ResultCode::ProviderTimeout);
  error.details.message = std::move(message);
  error.details.stage = "mock.adapter.generate";
  error.source_ref.ref_type = "route";
  error.source_ref.ref_id = std::move(route_key);

  AdapterCallResult result;
  result.error = std::move(error);
  result.result_code = ResultCode::ProviderTimeout;
  return result;
}

struct AdapterBehavior {
  bool succeed = true;
  std::string content_payload;
  std::string provider_trace_id;
  std::string failure_message;
  std::uint32_t prompt_tokens = 0U;
  std::uint32_t completion_tokens = 0U;
  std::uint32_t cache_hit_tokens = 0U;
  std::uint32_t cache_miss_tokens = 0U;
};

AdapterBehavior make_failure_behavior(std::string failure_message) {
  return AdapterBehavior{
    .succeed = false,
    .content_payload = {},
    .provider_trace_id = {},
    .failure_message = std::move(failure_message),
    .prompt_tokens = 0U,
    .completion_tokens = 0U,
    .cache_hit_tokens = 0U,
    .cache_miss_tokens = 0U,
  };
}

AdapterBehavior make_success_behavior(std::string content_payload,
                    std::string provider_trace_id,
                    std::uint32_t prompt_tokens,
                    std::uint32_t completion_tokens,
                    std::uint32_t cache_hit_tokens,
                    std::uint32_t cache_miss_tokens) {
  return AdapterBehavior{
    .succeed = true,
    .content_payload = std::move(content_payload),
    .provider_trace_id = std::move(provider_trace_id),
    .failure_message = {},
    .prompt_tokens = prompt_tokens,
    .completion_tokens = completion_tokens,
    .cache_hit_tokens = cache_hit_tokens,
    .cache_miss_tokens = cache_miss_tokens,
  };
}

void configure_adapter(const std::shared_ptr<MockLLMAdapter>& adapter,
                       std::string route_key,
                       AdapterBehavior behavior) {
  adapter->set_generate_handler(
      [route_key = std::move(route_key), behavior = std::move(behavior)](
          const LLMRequest& request) {
        if (!behavior.succeed) {
          return make_transport_failure(route_key, behavior.failure_message);
        }

        return make_success_result(request,
                                   behavior.content_payload,
                                   behavior.provider_trace_id,
                                   behavior.prompt_tokens,
                                   behavior.completion_tokens,
                                   behavior.cache_hit_tokens,
                                   behavior.cache_miss_tokens);
      });
}

struct FallbackRunArtifacts {
  LLMManagerResult result;
  std::shared_ptr<RecordingLogger> logger;
  std::shared_ptr<RecordingMeter> meter;
  std::shared_ptr<RecordingMetricsProvider> metrics_provider;
  std::shared_ptr<RecordingTracer> tracer;
  std::shared_ptr<MockLLMAdapter> cloud_adapter;
  std::shared_ptr<MockLLMAdapter> lan_adapter;
  std::shared_ptr<MockLLMAdapter> local_adapter;
};

FallbackRunArtifacts run_fallback_case(const LLMGenerateRequest& request,
                                       AdapterBehavior cloud_behavior,
                                       AdapterBehavior lan_behavior,
                                       AdapterBehavior local_behavior) {
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
      std::make_shared<const ProviderCatalogSnapshot>(make_fallback_catalog());

  assert_true(
      registry->init(dasall::llm::route::AdapterRegistryConfig{
          .blocked_failure_threshold = 6U,
      }),
      "LLM fallback integration should initialize AdapterRegistry before manager wiring");

  auto cloud_adapter = std::make_shared<MockLLMAdapter>();
  auto lan_adapter = std::make_shared<MockLLMAdapter>();
  auto local_adapter = std::make_shared<MockLLMAdapter>();

  configure_adapter(cloud_adapter, std::string(kCloudRoute), std::move(cloud_behavior));
  configure_adapter(lan_adapter, std::string(kLanRoute), std::move(lan_behavior));
  configure_adapter(local_adapter, std::string(kLocalRoute), std::move(local_behavior));

  assert_true(registry->register_adapter(make_registration(
                  "deepseek-prod",
                  "deepseek-chat",
                  "deepseek-chat-cloud",
                  "cloud",
                  {"cloud", "external", "deepseek"},
                  cloud_adapter)),
              "LLM fallback integration should register the primary cloud route");
  assert_true(registry->register_adapter(make_registration(
                  "lan-ollama",
                  "lan-general",
                  "ollama-lan",
                  "lan",
                  {"lan", "internal"},
                  lan_adapter)),
              "LLM fallback integration should register the explicit LAN fallback route");
  assert_true(registry->register_adapter(make_registration(
                  "local-runtime",
                  "local-small",
                  "runtime-local",
                  "local",
                  {"local", "internal"},
                  local_adapter)),
              "LLM fallback integration should register the local degrade-chain route");

  LLMManager manager(prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
                     catalog_snapshot,
                     nullptr,
                     metrics_bridge,
                     trace_bridge);
  assert_true(manager.init(make_config()),
              "LLM fallback integration should initialize LLMManager with real PromptPipeline and observability bridges");

  return FallbackRunArtifacts{
      .result = manager.generate(request),
      .logger = logger,
      .meter = meter,
      .metrics_provider = metrics_provider,
      .tracer = tracer,
      .cloud_adapter = cloud_adapter,
      .lan_adapter = lan_adapter,
      .local_adapter = local_adapter,
  };
}

void assert_attempted_routes(const std::vector<std::string>& actual_routes,
                             const std::vector<std::string>& expected_routes,
                             std::string_view message) {
  assert_equal(dasall::llm::test_support::join_routes(expected_routes),
               dasall::llm::test_support::join_routes(actual_routes),
               std::string(message));
}

void test_llm_fallback_integration_uses_lan_after_cloud_failure() {
  const auto artifacts = run_fallback_case(
      make_request("req-031-lan", "call-031-lan"),
      make_failure_behavior("cloud route timed out"),
      make_success_behavior("lan-fallback-success",
                            "trace-031-lan",
                            72U,
                            28U,
                            8U,
                            64U),
      make_success_behavior("local-should-not-run",
                            "trace-031-local-unused",
                            32U,
                            12U,
                            0U,
                            32U));

  if (!(artifacts.result.has_consistent_values() && artifacts.result.response.has_value())) {
    throw std::runtime_error(
        "LAN fallback path did not succeed: " + describe_result(artifacts.result));
  }

  assert_equal(std::string(kLanRoute),
               artifacts.result.resolved_route,
               "LLM fallback integration should surface the LAN route as the winning fallback");
  assert_attempted_routes(artifacts.result.attempted_routes,
                          {std::string(kCloudRoute), std::string(kLanRoute)},
                          "LLM fallback integration should record cloud then LAN in attempted_routes");
  assert_true(artifacts.result.fallback_used,
              "LLM fallback integration should mark fallback_used when LAN succeeds after cloud failure");
  assert_equal(1, artifacts.cloud_adapter->generate_call_count(),
               "LLM fallback integration should try the cloud route exactly once when retry_budget is zero");
  assert_equal(1, artifacts.lan_adapter->generate_call_count(),
               "LLM fallback integration should invoke the LAN fallback route after cloud failure");
  assert_equal(0, artifacts.local_adapter->generate_call_count(),
               "LLM fallback integration should stop the route chain after the LAN fallback succeeds");
  assert_true(artifacts.result.response->model_name.has_value() &&
                  *artifacts.result.response->model_name == "lan-general",
              "LLM fallback integration should preserve the LAN model name on the normalized response");
  assert_true(has_result_tag(artifacts.result, "route=lan-ollama/lan-general") &&
                  has_result_tag(artifacts.result, "provider_trace_id=trace-031-lan") &&
                  has_result_tag(artifacts.result, "selection_reason=fallback_chain_prepared"),
              "LLM fallback integration should expose the resolved LAN route, provider trace and prepared fallback chain through response tags");

  assert_true(artifacts.logger->events.size() == 1U,
              "LLM fallback integration should emit one degraded structured log entry for the LAN fallback path");
  const auto& log_event = artifacts.logger->events.front();
  assert_true(log_event.level == LogLevel::Warn &&
                  find_log_attr(log_event, "resolved_route") != nullptr &&
                  *find_log_attr(log_event, "resolved_route") == kLanRoute &&
                  *find_log_attr(log_event, "fallback_used") == "true" &&
                  *find_log_attr(log_event, "outcome") == "degraded" &&
                  *find_log_attr(log_event, "from_route") == kCloudRoute &&
                  *find_log_attr(log_event, "to_route") == kLanRoute &&
                  find_log_attr(log_event, "selection_reason_codes") != nullptr &&
                  find_log_attr(log_event, "selection_reason_codes")
                          ->find("fallback_chain_prepared") != std::string::npos,
              "LLM fallback integration should project fallback transition facts into structured logging");

  const auto* fallback_sample = find_sample(artifacts.meter->recorded_samples,
                                            "llm_fallback_total");
  assert_true(artifacts.metrics_provider->last_scope.name == "llm.observability" &&
                  fallback_sample != nullptr &&
                  fallback_sample->labels.stage ==
                      "fallback/planning/deepseek-prod/deepseek-chat_to_lan-ollama/lan-general" &&
                  fallback_sample->labels.outcome == "degraded",
              "LLM fallback integration should emit the fallback metric family with the cloud to LAN transition encoded in labels");

  assert_equal(3, static_cast<int>(artifacts.tracer->started_spans.size()),
               "LLM fallback integration should keep the three frozen llm trace spans on the degraded success path");
  assert_true(trace_attr_as_string(artifacts.tracer->started_spans.at(0).descriptor.attrs,
                                   "selection_reason_codes")
                      .value_or(std::string())
                      .find("fallback_chain_prepared") != std::string::npos &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "resolved_route") ==
                      std::optional<std::string>(std::string(kLanRoute)) &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "outcome") ==
                      std::optional<std::string>("degraded"),
              "LLM fallback integration should retain fallback chain and degraded outcome facts on trace attributes");
}

void test_llm_fallback_integration_reaches_local_after_cloud_and_lan_failures() {
  const auto artifacts = run_fallback_case(
      make_request("req-031-local", "call-031-local"),
      make_failure_behavior("cloud route timed out"),
      make_failure_behavior("lan route timed out"),
      make_success_behavior("local-fallback-success",
                            "trace-031-local",
                            48U,
                            18U,
                            0U,
                            48U));

  if (!(artifacts.result.has_consistent_values() && artifacts.result.response.has_value())) {
    throw std::runtime_error(
        "Local fallback path did not succeed: " + describe_result(artifacts.result));
  }

  assert_equal(std::string(kLocalRoute),
               artifacts.result.resolved_route,
               "LLM fallback integration should surface the local route after cloud and LAN both fail");
  assert_attempted_routes(artifacts.result.attempted_routes,
                          {std::string(kCloudRoute),
                           std::string(kLanRoute),
                           std::string(kLocalRoute)},
                          "LLM fallback integration should retain the full cloud to LAN to local attempted route chain");
  assert_true(artifacts.result.fallback_used,
              "LLM fallback integration should keep fallback_used true when the local degrade-chain route succeeds");
  assert_equal(1, artifacts.cloud_adapter->generate_call_count(),
               "LLM fallback integration should try the cloud route once before degrading");
  assert_equal(1, artifacts.lan_adapter->generate_call_count(),
               "LLM fallback integration should try the explicit LAN fallback before the local degrade-chain route");
  assert_equal(1, artifacts.local_adapter->generate_call_count(),
               "LLM fallback integration should invoke the local degrade-chain route after LAN failure");
  assert_true(artifacts.result.response->model_name.has_value() &&
                  *artifacts.result.response->model_name == "local-small",
              "LLM fallback integration should preserve the local model name on the normalized response");
  assert_true(has_result_tag(artifacts.result, "route=local-runtime/local-small") &&
                  has_result_tag(artifacts.result, "provider_trace_id=trace-031-local") &&
                  has_result_tag(artifacts.result, "selection_reason=fallback_chain_prepared"),
              "LLM fallback integration should expose the resolved local route and fallback-chain evidence through response tags");

  assert_true(artifacts.logger->events.size() == 1U,
              "LLM fallback integration should emit one degraded structured log entry for the local fallback path");
  const auto& log_event = artifacts.logger->events.front();
  assert_true(log_event.level == LogLevel::Warn &&
                  find_log_attr(log_event, "resolved_route") != nullptr &&
                  *find_log_attr(log_event, "resolved_route") == kLocalRoute &&
                  *find_log_attr(log_event, "fallback_used") == "true" &&
                  *find_log_attr(log_event, "outcome") == "degraded" &&
                  *find_log_attr(log_event, "from_route") == kCloudRoute &&
                  *find_log_attr(log_event, "to_route") == kLocalRoute,
              "LLM fallback integration should project cloud to local degrade facts into structured logging");

  const auto* fallback_sample = find_sample(artifacts.meter->recorded_samples,
                                            "llm_fallback_total");
  assert_true(fallback_sample != nullptr &&
                  fallback_sample->labels.stage ==
                      "fallback/planning/deepseek-prod/deepseek-chat_to_local-runtime/local-small" &&
                  fallback_sample->labels.outcome == "degraded",
              "LLM fallback integration should emit a fallback metric for the cloud to local degrade-chain transition");

  assert_equal(3, static_cast<int>(artifacts.tracer->started_spans.size()),
               "LLM fallback integration should keep the frozen llm trace spans when the local route wins");
  assert_true(trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                   "resolved_route") ==
                      std::optional<std::string>(std::string(kLocalRoute)) &&
                  trace_attr_as_string(artifacts.tracer->started_spans.at(2).descriptor.attrs,
                                       "outcome") ==
                      std::optional<std::string>("degraded"),
              "LLM fallback integration should keep the final local route and degraded outcome on the response-normalize span");
}

void test_llm_fallback_integration_surfaces_fallback_exhausted_after_all_routes_fail() {
  const auto artifacts = run_fallback_case(
      make_request("req-031-exhausted", "call-031-exhausted"),
      make_failure_behavior("cloud route timed out"),
      make_failure_behavior("lan route timed out"),
      make_failure_behavior("local route timed out"));

  if (!artifacts.result.has_consistent_values()) {
    throw std::runtime_error(
        "Fallback exhausted path returned inconsistent result: " +
        describe_result(artifacts.result));
  }

  assert_true(!artifacts.result.response.has_value(),
              "LLM fallback integration should not surface a response when every route fails");
  if (!(artifacts.result.code == std::optional<ResultCode>(ResultCode::ProviderTimeout) &&
      artifacts.result.failure_category ==
          std::optional<LLMFailureCategory>(LLMFailureCategory::FallbackExhausted) &&
      artifacts.result.error.has_value() &&
      artifacts.result.error->details.message == "local route timed out" &&
      artifacts.result.error->details.stage == "llm.manager.execute_unary")) {
    throw std::runtime_error(
        "LLM fallback integration should elevate the final transport failure into FallbackExhausted while preserving the last route error details: " +
        describe_result(artifacts.result));
  }
  assert_equal(std::string(kLocalRoute),
               artifacts.result.resolved_route,
               "LLM fallback integration should surface the last attempted route on the exhausted failure path");
  assert_attempted_routes(artifacts.result.attempted_routes,
                          {std::string(kCloudRoute),
                           std::string(kLanRoute),
                           std::string(kLocalRoute)},
                          "LLM fallback integration should keep the full attempted route chain on fallback exhausted");
  assert_true(!artifacts.result.fallback_used,
              "LLM fallback integration should keep fallback_used false when no fallback route ultimately succeeds");
  assert_equal(1, artifacts.cloud_adapter->generate_call_count(),
               "LLM fallback integration should attempt the cloud route once on the exhausted path");
  assert_equal(1, artifacts.lan_adapter->generate_call_count(),
               "LLM fallback integration should attempt the LAN route once on the exhausted path");
  assert_equal(1, artifacts.local_adapter->generate_call_count(),
               "LLM fallback integration should attempt the local route once before surfacing fallback exhausted");
    assert_true(artifacts.logger->events.size() == 1U,
          "LLM fallback integration should emit one structured failure log when fallback is exhausted");
    const auto& log_event = artifacts.logger->events.front();
    assert_true(log_event.level == LogLevel::Error &&
            find_log_attr(log_event, "request_id") != nullptr &&
            *find_log_attr(log_event, "request_id") == "req-031-exhausted" &&
            *find_log_attr(log_event, "llm_call_id") == "call-031-exhausted" &&
            *find_log_attr(log_event, "request_mode") == "unary" &&
            *find_log_attr(log_event, "resolved_route") == kLocalRoute &&
            *find_log_attr(log_event, "outcome") == "failure" &&
            *find_log_attr(log_event, "failure_category") == "fallback_exhausted" &&
            *find_log_attr(log_event, "result_code") == "4001" &&
            *find_log_attr(log_event, "result_code_category") == "provider" &&
            *find_log_attr(log_event, "error_stage") == "llm.manager.execute_unary" &&
            *find_log_attr(log_event, "error_message") == "local route timed out" &&
            *find_log_attr(log_event, "attempted_routes") ==
              "deepseek-prod/deepseek-chat,lan-ollama/lan-general,local-runtime/local-small" &&
            *find_log_attr(log_event, "route_attempt_count") == "3" &&
            *find_log_attr(log_event, "from_route") == kCloudRoute &&
            *find_log_attr(log_event, "to_route") == kLocalRoute,
          "LLM fallback integration should keep failure result, final route and attempted route chain in structured logging");

    const auto* calls_sample = find_sample(artifacts.meter->recorded_samples,
                       "llm_calls_total");
    const auto* fallback_sample = find_sample(artifacts.meter->recorded_samples,
                        "llm_fallback_total");
    const auto* adapter_timeout_sample = find_sample(artifacts.meter->recorded_samples,
                             "llm_adapter_timeout_total");
    assert_true(calls_sample != nullptr && fallback_sample != nullptr &&
            adapter_timeout_sample != nullptr &&
            calls_sample->labels.outcome == "failure" &&
            fallback_sample->labels.outcome == "failure" &&
            adapter_timeout_sample->labels.error_code == "provider",
          "LLM fallback integration should emit failure, fallback and adapter timeout metric anchors when every route fails");

    assert_equal(1, static_cast<int>(artifacts.tracer->started_spans.size()),
           "LLM fallback integration should emit one failure trace span when fallback is exhausted");
    assert_equal(std::string("llm.adapter.invoke"),
           artifacts.tracer->started_spans.front().descriptor.name,
           "LLM fallback exhausted trace should point at adapter invocation as the failing stage");
    assert_true(trace_attr_as_string(artifacts.tracer->started_spans.front().descriptor.attrs,
                     "outcome") ==
              std::optional<std::string>("failure") &&
            trace_attr_as_string(artifacts.tracer->started_spans.front().descriptor.attrs,
                       "result_code") ==
              std::optional<std::string>("4001") &&
            trace_attr_as_string(artifacts.tracer->started_spans.front().descriptor.attrs,
                       "attempted_routes") ==
              std::optional<std::string>(
                "deepseek-prod/deepseek-chat,lan-ollama/lan-general,local-runtime/local-small"),
          "LLM fallback exhausted trace should preserve low-cardinality failure and route-chain attributes");
}

}  // namespace

int main() {
  try {
    test_llm_fallback_integration_uses_lan_after_cloud_failure();
    test_llm_fallback_integration_reaches_local_after_cloud_and_lan_failures();
    test_llm_fallback_integration_surfaces_fallback_exhausted_after_all_routes_fail();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
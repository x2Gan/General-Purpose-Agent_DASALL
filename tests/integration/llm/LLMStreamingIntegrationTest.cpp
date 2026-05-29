#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/observability/LLMMetricsBridge.h"
#include "../../../llm/src/observability/LLMTraceBridge.h"
#include "../../../llm/src/prompt/PromptPipeline.h"
#include "../../../llm/src/stream/IStreamObserver.h"

#include "../../mocks/include/MockLLMAdapter.h"
#include "LLMIntegrationTestSupport.h"
#include "../../unit/llm/ModelRouterTestSupport.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::IStreamObserver;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ModelSelectionHint;
using dasall::llm::StreamSessionRef;
using dasall::llm::observability::LLMMetricsBridge;
using dasall::llm::observability::LLMTraceBridge;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::route::AdapterRegistration;
using dasall::tests::integration::llm_support::RecordingLogger;
using dasall::tests::integration::llm_support::RecordingMeter;
using dasall::tests::integration::llm_support::RecordingMetricsProvider;
using dasall::tests::integration::llm_support::RecordingTracer;
using dasall::tests::integration::llm_support::find_log_attr;
using dasall::tests::integration::llm_support::find_sample;
using dasall::tests::integration::llm_support::trace_attr_as_string;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kPromptAssetRoot = "/home/gangan/DASALL/llm/assets/prompts";

bool has_result_tag(const dasall::llm::LLMManagerResult& result,
                    const std::string& expected_tag) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find(result.response->tags->begin(),
                   result.response->tags->end(),
                   expected_tag) != result.response->tags->end();
}

bool has_result_tag_prefix(const dasall::llm::LLMManagerResult& result,
                           const std::string& prefix) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find_if(result.response->tags->begin(),
                      result.response->tags->end(),
                      [&](const std::string& tag) {
                        return tag.rfind(prefix, 0U) == 0U;
                      }) != result.response->tags->end();
}

AdapterRegistration make_streaming_registration(std::shared_ptr<MockLLMAdapter> adapter) {
  return AdapterRegistration{
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-chat",
      .adapter_id = "deepseek-cloud",
      .deployment_type = "cloud",
      .capability_tags = {"cloud", "external", "streaming"},
      .supports_streaming = true,
      .adapter = std::move(adapter),
  };
}

LLMSubsystemConfig make_config() {
  auto config = dasall::llm::test_support::make_config(
      "planning", "cloud.reasoning", std::nullopt, {"local.small"}, false, false);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = std::string(kPromptAssetRoot);
  config.prompt_selector_overlay.active_scene = "general";
  config.prompt_selector_overlay.active_persona = "planner";
  config.stage_routes["planning"].streaming_enabled = true;
  config.timeout_policy.retry_budget = 0U;
  return config;
}

LLMGenerateRequest make_request() {
  LLMRequest request;
  request.request_id = "req-031-stream";
  request.llm_call_id = "call-031-stream";
  request.model_route = "cloud.reasoning";
  request.request_mode = dasall::contracts::LLMRequestMode::Streaming;
  request.messages = std::vector<std::string>{"draft the streaming integration acceptance"};
  request.created_at = 1712966405000LL;
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
  request.tags = std::vector<std::string>{"integration", "llm-streaming"};

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
          .requires_tools = true,
          .requires_reasoning = false,
          .prefers_visible_reasoning = false,
          .estimated_input_tokens = 2048U,
          .target_output_tokens = 1024U,
          .previous_route_failures = 0U,
      }),
  };
}

void test_llm_streaming_integration_returns_normalized_success_and_usage_tags() {
  auto prompt_pipeline = std::make_shared<PromptPipeline>();
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<dasall::llm::execution::ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto logger = std::make_shared<RecordingLogger>();
  auto meter = std::make_shared<RecordingMeter>();
  auto metrics_provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto tracer = std::make_shared<RecordingTracer>();
  auto metrics_bridge = std::make_shared<LLMMetricsBridge>(logger, metrics_provider);
  auto trace_bridge = std::make_shared<LLMTraceBridge>(tracer);
  auto catalog_snapshot =
      std::make_shared<const dasall::llm::provider::ProviderCatalogSnapshot>(
          dasall::llm::test_support::make_default_catalog());

  assert_true(registry->init(dasall::llm::route::AdapterRegistryConfig{
                  .blocked_failure_threshold = 6U,
              }),
              "LLM streaming integration should initialize AdapterRegistry before wiring the manager");

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_stream_handler([](const LLMRequest& request, IStreamObserver* observer) {
    const StreamSessionRef session_ref{
        .session_id = std::string("mock-stream:") +
                      request.llm_call_id.value_or(std::string("unknown")),
    };
    if (observer == nullptr) {
      return StreamSessionRef{};
    }

    if (!observer->on_stream_session_started(session_ref).proceed) {
      return session_ref;
    }
    if (!observer->on_stream_delta("stream-").proceed) {
      return session_ref;
    }
    if (!observer->on_stream_delta("ok").proceed) {
      return session_ref;
    }

    LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = LLMResponseKind::DirectResponse;
    response.content_payload = "stream-ok";
    response.finish_reason = "stop";

    AdapterCallResult result;
    result.response = std::move(response);
    result.usage = AdapterUsageFragment{
        .prompt_tokens = 64U,
        .completion_tokens = 32U,
        .total_tokens = 96U,
        .prompt_cache_hit_tokens = 16U,
        .prompt_cache_miss_tokens = 48U,
    };
    result.provider_diagnostics.provider_trace_id = "trace-031-stream";
    result.provider_diagnostics.audit_tags = {"normalized"};
    observer->on_stream_completed(result);
    return session_ref;
  });
  assert_true(registry->register_adapter(make_streaming_registration(adapter)),
              "LLM streaming integration should register a streaming-capable MockLLMAdapter for the selected route");

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
              "LLM streaming integration should initialize LLMManager with a real PromptPipeline and streaming-enabled route config");

  const auto result = manager.stream_generate(make_request(), nullptr);

  assert_true(result.has_consistent_values() && result.response.has_value(),
              "LLM streaming integration should return a consistent success result across PromptPipeline, routing, stream execution and response normalization");
  assert_equal(std::string("deepseek-prod/deepseek-chat"), result.resolved_route,
               "LLM streaming integration should resolve the concrete streaming route through ModelRouter");
  assert_equal(1, static_cast<int>(result.attempted_routes.size()),
               "LLM streaming integration should complete on the primary route without fallback");
  assert_true(!result.fallback_used,
              "LLM streaming integration should keep fallback_used false on the streaming happy path");
  assert_true(!result.failure_category.has_value(),
              "LLM streaming integration should not attach a failure category on success");
  assert_true(result.response->content_payload.has_value() &&
                  *result.response->content_payload == "stream-ok",
              "LLM streaming integration should preserve the terminal adapter payload after response normalization");
  assert_true(result.response->prompt_id.has_value() &&
                  *result.response->prompt_id == "planner" &&
                  result.response->prompt_version.has_value() &&
                  *result.response->prompt_version == "2026.04.11",
              "LLM streaming integration should stamp the selected prompt asset identity onto the normalized response");
  assert_true(result.response->model_name.has_value() &&
                  *result.response->model_name == "deepseek-chat",
              "LLM streaming integration should preserve the resolved provider model on the normalized response");
  assert_true(has_result_tag(result, "route=deepseek-prod/deepseek-chat") &&
                  has_result_tag(result, "provider_trace_id=trace-031-stream") &&
                  has_result_tag(result, "audit=provider_audit:normalized") &&
                  has_result_tag(result, "usage:prompt_cache_hit_tokens=16") &&
                  has_result_tag(result, "usage:prompt_cache_miss_tokens=48") &&
                  has_result_tag_prefix(result, "usage:estimated_cost_usd="),
              "LLM streaming integration should surface route, provider trace, audit and usage anchors through provider-neutral response tags");
  assert_equal(0, adapter->generate_call_count(),
               "LLM streaming integration should not route streaming requests through unary generate()");
  assert_equal(1, adapter->stream_generate_call_count(),
               "LLM streaming integration should invoke adapter stream_generate() exactly once");
  assert_true(adapter->last_stream_request().has_value() &&
                  adapter->last_stream_request()->request_mode.has_value() &&
                  *adapter->last_stream_request()->request_mode ==
              dasall::contracts::LLMRequestMode::Streaming &&
            adapter->last_stream_request()->model_route.has_value() &&
            *adapter->last_stream_request()->model_route ==
              "deepseek-prod/deepseek-chat" &&
                   adapter->last_stream_request()->prompt_id.has_value() &&
                   *adapter->last_stream_request()->prompt_id == "planner" &&
                   adapter->last_stream_request()->prompt_version.has_value() &&
                   *adapter->last_stream_request()->prompt_version == "2026.04.11" &&
                   adapter->last_stream_request()->messages.has_value() &&
                   adapter->last_stream_request()->messages->size() == 2U,
              "LLM streaming integration should hand PromptPipeline output and the concrete route into MockLLMAdapter::stream_generate");

            assert_true(logger->events.size() == 1U,
                  "LLM streaming integration should emit one structured call log on the streaming success path");
            const auto& log_event = logger->events.front();
            assert_true(find_log_attr(log_event, "request_mode") != nullptr &&
                    *find_log_attr(log_event, "request_mode") == "streaming" &&
                    *find_log_attr(log_event, "request_id") == "req-031-stream" &&
                    *find_log_attr(log_event, "llm_call_id") == "call-031-stream" &&
                    *find_log_attr(log_event, "resolved_route") ==
                      "deepseek-prod/deepseek-chat" &&
                    *find_log_attr(log_event, "outcome") == "success" &&
                    *find_log_attr(log_event, "prompt_cache_hit_tokens") == "16" &&
                    *find_log_attr(log_event, "prompt_cache_miss_tokens") == "48",
                  "LLM streaming integration should preserve request identity, route, mode and usage fields in structured logging");
            assert_true(find_sample(meter->recorded_samples, "llm_calls_total") != nullptr &&
                    find_sample(meter->recorded_samples, "llm_call_latency_ms") != nullptr &&
                    metrics_provider->last_scope.name == "llm.observability",
                  "LLM streaming integration should emit the core llm observability metric anchors");
            assert_equal(3, static_cast<int>(tracer->started_spans.size()),
                   "LLM streaming integration should emit route, adapter and normalize trace spans");
            assert_true(trace_attr_as_string(tracer->started_spans.at(1).descriptor.attrs,
                             "request_mode") ==
                      std::optional<std::string>("streaming") &&
                    trace_attr_as_string(tracer->started_spans.at(2).descriptor.attrs,
                               "outcome") ==
                      std::optional<std::string>("success"),
                  "LLM streaming integration should keep request mode and outcome visible on trace attributes");
}

}  // namespace

int main() {
  try {
    test_llm_streaming_integration_returns_normalized_success_and_usage_tags();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

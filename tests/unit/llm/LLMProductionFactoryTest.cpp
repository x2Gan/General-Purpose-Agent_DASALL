#include <algorithm>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/include/ILLMTransport.h"
#include "../../../llm/include/LLMProductionFactory.h"
#include "../../../llm/include/route/ModelSelectionHint.h"
#include "../../../profiles/include/RuntimePolicySnapshot.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::LLMResponseKind;
using dasall::llm::ILLMTransport;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMProductionFactoryOptions;
using dasall::llm::LLMTransportRequest;
using dasall::llm::LLMTransportResponse;
using dasall::llm::ModelSelectionHint;
using dasall::llm::create_production_llm_manager;
using dasall::profiles::CapabilityCachePolicy;
using dasall::profiles::DegradePolicy;
using dasall::profiles::ExecutionPolicy;
using dasall::profiles::ModelProfile;
using dasall::profiles::ModelRoutePolicy;
using dasall::profiles::OpsPolicy;
using dasall::profiles::PromptPolicy;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::profiles::TimeoutBudget;
using dasall::profiles::TimeoutPolicy;
using dasall::profiles::TokenBudgetPolicy;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

std::string repo_provider_catalog_root() {
  return (std::filesystem::path(DASALL_REPO_ROOT) / "llm/assets/providers").generic_string();
}

std::string describe_result(const dasall::llm::LLMManagerResult& result) {
  std::string description = "resolved_route=" + result.resolved_route;

  description += ", attempted_routes=";
  for (std::size_t index = 0; index < result.attempted_routes.size(); ++index) {
    if (index != 0U) {
      description += ",";
    }
    description += result.attempted_routes[index];
  }

  description += ", fallback_used=";
  description += result.fallback_used ? "true" : "false";

  if (result.code.has_value()) {
    description += ", code=" + std::to_string(static_cast<int>(*result.code));
  }

  if (result.failure_category.has_value()) {
    description += ", failure_category=" +
                   std::to_string(static_cast<int>(*result.failure_category));
  }

  if (result.error.has_value()) {
    description += ", error_stage=" + result.error->details.stage;
    description += ", error_message=" + result.error->details.message;
  }

  return description;
}

class StubTransport final : public ILLMTransport {
 public:
  void push_response(std::string url_fragment, LLMTransportResponse response) {
    for (auto& stub : stubs_) {
      if (stub.url_fragment == url_fragment) {
        stub.responses.push_back(std::move(response));
        return;
      }
    }

    Stub stub;
    stub.url_fragment = std::move(url_fragment);
    stub.responses.push_back(std::move(response));
    stubs_.push_back(std::move(stub));
  }

  [[nodiscard]] LLMTransportResponse send(const LLMTransportRequest& request) override {
    requests.push_back(request);

    for (auto& stub : stubs_) {
      if (request.url.find(stub.url_fragment) == std::string::npos) {
        continue;
      }

      if (stub.responses.empty()) {
        break;
      }

      auto response = stub.responses.front();
      stub.responses.pop_front();
      return response;
    }

    return LLMTransportResponse{
        .status_code = 0U,
        .body = {},
        .error_message = "missing transport stub for " + request.url,
    };
  }

  std::vector<LLMTransportRequest> requests;

 private:
  struct Stub {
    std::string url_fragment;
    std::deque<LLMTransportResponse> responses;
  };

  std::vector<Stub> stubs_;
};

RuntimePolicySnapshot make_snapshot(std::string primary_route,
                                    std::optional<std::string> fallback_route,
                                    std::vector<std::string> degrade_chain) {
  return RuntimePolicySnapshot{
      42U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = 5000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"planning",
             ModelRoutePolicy{.route = std::move(primary_route),
                    .fallback_route = std::move(fallback_route),
                                .streaming_enabled = false}},
              {"response",
               ModelRoutePolicy{.route = "cloud.general",
                                .fallback_route = std::string("local.small"),
                                .streaming_enabled = true}},
          },
      },
      TokenBudgetPolicy{.max_input_tokens = 4096U,
                        .max_output_tokens = 1024U,
                        .max_history_turns = 8U,
                        .compression_threshold = 3000U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 1000,
                            .expire_after_ms = 5000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 500},
      DegradePolicy{.fallback_chain = std::move(degrade_chain),
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 4000,
                                         .retry_budget = 0U,
                                         .circuit_breaker_threshold = 4U},
                    .tool = TimeoutBudget{.timeout_ms = 1500,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 3U},
                    .mcp = TimeoutBudget{.timeout_ms = 1500,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .workflow = TimeoutBudget{.timeout_ms = 3000,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 3U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin", "mcp"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "full",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = true,
                .upgrade_strategy = "rolling"},
      3U,
      false,
  };
}

LLMGenerateRequest make_request() {
  LLMRequest request;
  request.request_id = "req-prod-factory-001";
  request.llm_call_id = "call-prod-factory-001";
  request.model_route = "cloud.reasoning";
  request.request_mode = LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"summarize the current fallback route"};
  request.created_at = 1712966405000LL;
  request.output_schema_ref = "schema://planner/default";
  request.response_format = "json_object";
  request.max_output_tokens = 128U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{"unit", "production-factory"};

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
          .estimated_input_tokens = 1024U,
          .target_output_tokens = 512U,
          .previous_route_failures = 0U,
      }),
  };
}

bool requested_url_fragment(const std::vector<LLMTransportRequest>& requests,
                            std::string_view fragment) {
  return std::any_of(requests.begin(), requests.end(), [&](const auto& request) {
    return request.url.find(fragment) != std::string::npos;
  });
}

void test_production_factory_routes_cloud_openai_provider() {
  auto transport = std::make_shared<StubTransport>();
  transport->push_response(
      "api.deepseek.com/chat/completions",
      LLMTransportResponse{
          .status_code = 200U,
          .body = R"({
        "id":"chatcmpl-prod-001",
        "model":"deepseek-reasoner",
        "choices":[{
          "message":{
            "role":"assistant",
            "content":"{\"route\":\"cloud\"}",
            "reasoning_content":"reasoning-only"
          },
          "finish_reason":"stop"
        }],
        "usage":{
          "prompt_tokens":24,
          "completion_tokens":8,
          "total_tokens":32,
          "prompt_cache_hit_tokens":0,
          "prompt_cache_miss_tokens":24
        }
      })",
          .error_message = {},
      });

  const auto factory_result = create_production_llm_manager(
      make_snapshot("cloud.reasoning", std::string("lan.general"), {"lan.general", "local.small"}),
      LLMProductionFactoryOptions{.secret_backend = nullptr,
                    .transport = transport,
                    .provider_catalog_baseline_root = repo_provider_catalog_root()});
  assert_true(factory_result.ok(),
              "production factory should initialize when baseline catalog contains the openai-compatible cloud provider");

  const auto result = factory_result.manager->generate(make_request());
  assert_true(result.has_consistent_values() && result.response.has_value(),
              "production manager should return a shared response for the cloud primary route: " +
            describe_result(result));
  assert_true(!result.fallback_used,
              "production manager should not mark fallback_used when the cloud primary route succeeds immediately");
  assert_equal(std::string("deepseek-prod/deepseek-chat"), result.resolved_route,
               "production manager should resolve a registered deepseek cloud route for cloud.reasoning");
  assert_true(result.attempted_routes == std::vector<std::string>{"deepseek-prod/deepseek-chat"},
              "production manager should only attempt the cloud route when it succeeds on the first try");
  assert_equal(std::string("{\"route\":\"cloud\"}"), *result.response->content_payload,
               "production manager should surface the openai-compatible adapter payload for the cloud route");
  assert_equal(1, static_cast<int>(transport->requests.size()),
               "production manager should send exactly one transport call when the cloud route succeeds");
  assert_true(requested_url_fragment(transport->requests, "api.deepseek.com/chat/completions"),
              "production factory should register the openai-compatible cloud route");
}

void test_production_factory_routes_lan_ollama_provider() {
  auto transport = std::make_shared<StubTransport>();
  transport->push_response(
      "lan-ollama.internal:11434/api/chat",
      LLMTransportResponse{
          .status_code = 200U,
          .body = R"({
        "model":"lan-general",
        "created_at":"2026-05-16T09:30:00Z",
        "message":{
          "role":"assistant",
          "content":"{\"route\":\"lan\"}",
          "thinking":"kept-local"
        },
        "done_reason":"stop",
        "done":true,
        "prompt_eval_count":42,
        "eval_count":11
      })",
          .error_message = {},
      });

  const auto factory_result = create_production_llm_manager(
      make_snapshot("lan.general", std::string("local.small"), {"local.small"}),
      LLMProductionFactoryOptions{.secret_backend = nullptr,
                    .transport = transport,
                    .provider_catalog_baseline_root = repo_provider_catalog_root()});
  assert_true(factory_result.ok(),
              "production factory should initialize when baseline catalog contains the ollama-native LAN provider");

  const auto result = factory_result.manager->generate(make_request());
  assert_true(result.has_consistent_values() && result.response.has_value(),
              "production manager should return a shared response for the LAN primary route: " +
            describe_result(result));
  assert_true(!result.fallback_used,
              "production manager should not mark fallback_used when the LAN primary route succeeds immediately");
  assert_equal(std::string("lan-ollama/lan-general"), result.resolved_route,
               "production manager should resolve the LAN provider/model route for lan.general");
  assert_true(result.attempted_routes == std::vector<std::string>{"lan-ollama/lan-general"},
              "production manager should only attempt the LAN route when it succeeds on the first try");
  assert_equal(std::string("{\"route\":\"lan\"}"), *result.response->content_payload,
               "production manager should surface the LAN adapter payload for the LAN route");
  assert_equal(1, static_cast<int>(transport->requests.size()),
               "production manager should send exactly one transport call when the LAN route succeeds");
  assert_true(requested_url_fragment(transport->requests, "lan-ollama.internal:11434/api/chat"),
              "production factory should register the ollama-native LAN route from the baseline provider catalog");
}

void test_production_factory_routes_local_runtime_provider() {
  auto transport = std::make_shared<StubTransport>();
  transport->push_response(
      "local-runtime:///var/run/dasall/local-llm/generate",
      LLMTransportResponse{
          .status_code = 200U,
          .body = R"({
        "model":"local-small",
        "output_text":"{\"route\":\"local\"}",
        "finish_reason":"stop",
        "runtime_session_id":"local-session-7",
        "input_tokens":18,
        "output_tokens":6,
        "total_tokens":24,
        "reasoning_trace":"hidden-local"
      })",
          .error_message = {},
      });

  const auto factory_result = create_production_llm_manager(
      make_snapshot("local.small", std::nullopt, {"local.small"}),
      LLMProductionFactoryOptions{.secret_backend = nullptr,
                    .transport = transport,
                    .provider_catalog_baseline_root = repo_provider_catalog_root()});
  assert_true(factory_result.ok(),
              "production factory should initialize when baseline catalog contains the local runtime provider");

  const auto result = factory_result.manager->generate(make_request());
  assert_true(result.has_consistent_values() && result.response.has_value(),
              "production manager should return a shared response for the local primary route: " +
                  describe_result(result));
  assert_true(!result.fallback_used,
              "production manager should not mark fallback_used when the local primary route succeeds immediately");
  assert_equal(std::string("local-runtime/local-small"), result.resolved_route,
               "production manager should resolve the local runtime provider/model route for local.small");
  assert_true(result.attempted_routes == std::vector<std::string>{"local-runtime/local-small"},
              "production manager should only attempt the local route when it succeeds on the first try");
  assert_equal(std::string("{\"route\":\"local\"}"), *result.response->content_payload,
               "production manager should surface the local runtime adapter payload for the local route");
  assert_equal(1, static_cast<int>(transport->requests.size()),
               "production manager should send exactly one transport call when the local route succeeds");
  assert_true(requested_url_fragment(transport->requests, "local-runtime:///var/run/dasall/local-llm/generate"),
              "production factory should register the local runtime route from the baseline provider catalog");
}

}  // namespace

int main() {
  try {
    test_production_factory_routes_cloud_openai_provider();
    test_production_factory_routes_lan_ollama_provider();
    test_production_factory_routes_local_runtime_provider();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

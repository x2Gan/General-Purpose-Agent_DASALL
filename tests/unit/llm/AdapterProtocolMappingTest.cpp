#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/adapters/OllamaAdapter.h"
#include "../../../llm/src/adapters/OpenAICompatibleAdapter.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::ResultCode;
using dasall::llm::ILLMTransport;
using dasall::llm::LLMAdapterConfig;
using dasall::llm::LLMTransportMethod;
using dasall::llm::LLMTransportRequest;
using dasall::llm::LLMTransportResponse;
using dasall::llm::OllamaAdapter;
using dasall::llm::OpenAICompatibleAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class CapturingTransport final : public ILLMTransport {
 public:
  [[nodiscard]] LLMTransportResponse send(const LLMTransportRequest& request) override {
    ++call_count;
    last_request = request;
    return next_response;
  }

  LLMTransportResponse next_response;
  std::optional<LLMTransportRequest> last_request;
  int call_count = 0;
};

LLMAdapterConfig make_config() {
  return LLMAdapterConfig{
      .adapter_id = "deepseek-prod",
      .adapter_family = "openai_compatible",
      .provider_instance_id = "deepseek-prod",
      .base_url = "https://api.deepseek.example/v1",
      .base_url_alias = "deepseek/default",
      .auth_ref = "secret://llm/providers/deepseek-prod",
      .header_refs = {"header://llm/providers/deepseek-org"},
      .activation_flag = true,
      .snapshot_version = "2026.04.13",
      .timeout_ms = 4000U,
      .max_retries = 1U,
      .capability_tags = {"cloud", "reasoning"},
  };
}

LLMRequest make_request() {
  LLMRequest request;
  request.request_id = "req-025-001";
  request.llm_call_id = "call-025-001";
  request.model_route = "deepseek-prod/deepseek-chat";
  request.request_mode = LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"system: plan carefully",
                                              "user: diagnose timeout"};
  request.created_at = 1712966401000LL;
  request.prompt_id = "prompt.plan.default";
  request.prompt_version = "2026-04-13.1";
  request.response_format = "json_object";
  request.max_output_tokens = 256U;
  return request;
}

LLMAdapterConfig make_ollama_config() {
  return LLMAdapterConfig{
      .adapter_id = "ollama-lan",
      .adapter_family = "ollama_native",
      .provider_instance_id = "lan-ollama",
      .base_url = "http://lan-ollama.internal:11434",
      .base_url_alias = "lan/ollama-primary",
      .auth_ref = "profile://llm/providers/lan-ollama/noauth",
      .header_refs = {"header://llm/providers/lan-ollama-trace"},
      .activation_flag = true,
      .snapshot_version = "2026.04.13",
      .timeout_ms = 2500U,
      .max_retries = 1U,
      .capability_tags = {"lan", "internal"},
  };
}

LLMRequest make_ollama_request() {
  LLMRequest request;
  request.request_id = "req-026-001";
  request.llm_call_id = "call-026-001";
  request.model_route = "lan-ollama/qwen2.5:14b-instruct-q4_K_M";
  request.request_mode = LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"developer: answer in json",
                                              "user: summarize adapter state"};
  request.created_at = 1712966462000LL;
  request.prompt_id = "prompt.lan.default";
  request.prompt_version = "2026-04-13.2";
  request.response_format = "json_object";
  request.max_output_tokens = 96U;
  return request;
}

void test_openai_compatible_adapter_maps_unary_request_and_success_response() {
  auto transport = std::make_shared<CapturingTransport>();
  transport->next_response = LLMTransportResponse{
      .status_code = 200U,
      .body = R"({
        "id":"chatcmpl-42",
        "model":"deepseek-chat",
        "choices":[{
          "message":{
            "role":"assistant",
            "content":"fallback-ready",
            "reasoning_content":"reasoning-only"
          },
          "finish_reason":"stop"
        }],
        "usage":{
          "prompt_tokens":16,
          "completion_tokens":4,
          "total_tokens":20,
          "prompt_cache_hit_tokens":6,
          "prompt_cache_miss_tokens":10
        }
      })",
      .error_message = {},
  };

  OpenAICompatibleAdapter adapter(transport);
  assert_true(adapter.init(make_config()),
              "OpenAICompatibleAdapter should initialize with projected provider config and transport dependency");

  const auto result = adapter.generate(make_request());

  assert_true(transport->last_request.has_value(),
              "OpenAICompatibleAdapter should issue a concrete transport request during generate()");
  assert_equal(1, transport->call_count,
               "OpenAICompatibleAdapter should call transport exactly once for a unary request");
  assert_true(transport->last_request->method == LLMTransportMethod::Post,
              "OpenAICompatibleAdapter should map unary generate() to POST chat completions");
  assert_equal(std::string("https://api.deepseek.example/v1/chat/completions"),
               transport->last_request->url,
               "OpenAICompatibleAdapter should append /chat/completions to the projected base_url");
  assert_equal(std::string("secret://llm/providers/deepseek-prod"),
               transport->last_request->auth_ref,
               "OpenAICompatibleAdapter should forward auth_ref to transport instead of resolving secrets inside the adapter");
  assert_equal(std::string("deepseek/default"), transport->last_request->base_url_alias,
               "OpenAICompatibleAdapter should preserve base_url_alias for downstream endpoint indirection");
  assert_true(transport->last_request->body.find("\"model\":\"deepseek-chat\"") != std::string::npos,
              "OpenAICompatibleAdapter should derive the concrete model id from request.model_route");
  assert_true(transport->last_request->body.find("\"role\":\"system\"") != std::string::npos &&
                  transport->last_request->body.find("\"content\":\"plan carefully\"") != std::string::npos,
              "OpenAICompatibleAdapter should map prefixed system messages into OpenAI-compatible role/content objects");
  assert_true(transport->last_request->body.find("\"role\":\"user\"") != std::string::npos &&
                  transport->last_request->body.find("\"content\":\"diagnose timeout\"") != std::string::npos,
              "OpenAICompatibleAdapter should map prefixed user messages into OpenAI-compatible role/content objects");
  assert_true(transport->last_request->body.find("\"response_format\":{\"type\":\"json_object\"}") != std::string::npos,
              "OpenAICompatibleAdapter should map json_object response format into the OpenAI-compatible request body");

  assert_true(result.response.has_value(),
              "OpenAICompatibleAdapter should return a shared LLMResponse on successful 2xx provider responses");
  assert_equal(std::string("req-025-001"), *result.response->request_id,
               "OpenAICompatibleAdapter should preserve request_id in the shared response");
  assert_equal(std::string("call-025-001"), *result.response->llm_call_id,
               "OpenAICompatibleAdapter should preserve llm_call_id in the shared response");
  assert_equal(std::string("fallback-ready"), *result.response->content_payload,
               "OpenAICompatibleAdapter should map assistant message content into the shared response payload");
  assert_equal(std::string("deepseek-chat"), *result.response->model_name,
               "OpenAICompatibleAdapter should preserve provider model identity in the shared response");
  assert_equal(std::string("stop"), *result.response->finish_reason,
               "OpenAICompatibleAdapter should surface provider finish_reason for 022 to canonicalize");
  assert_true(result.usage.has_value() && result.usage->prompt_tokens == 16U &&
                  result.usage->completion_tokens == 4U && result.usage->total_tokens == 20U,
              "OpenAICompatibleAdapter should project provider usage into AdapterUsageFragment for 023 aggregation");
  assert_equal(std::string("chatcmpl-42"), result.provider_diagnostics.provider_trace_id,
               "OpenAICompatibleAdapter should keep provider trace id in diagnostics rather than shared contracts");
  assert_equal(std::string("reasoning-only"), result.provider_diagnostics.reasoning_content,
               "OpenAICompatibleAdapter should keep provider-private reasoning_content in diagnostics so 022 can strip it");
}

void test_openai_compatible_adapter_surfaces_transport_failures_without_throwing() {
  auto transport = std::make_shared<CapturingTransport>();
  transport->next_response = LLMTransportResponse{
      .status_code = 503U,
      .body = {},
      .error_message = {},
  };

  OpenAICompatibleAdapter adapter(transport);
  assert_true(adapter.init(make_config()),
              "OpenAICompatibleAdapter should initialize before transport failure coverage");

  const auto result = adapter.generate(make_request());

  assert_true(!result.response.has_value() && result.error.has_value() && result.result_code.has_value(),
              "OpenAICompatibleAdapter should surface transport failures through AdapterCallResult error fields instead of exceptions");
  assert_true(*result.result_code == ResultCode::ProviderTimeout,
              "OpenAICompatibleAdapter should map retryable HTTP transport failures to ProviderTimeout in the current shared code set");
  assert_true(result.error->retryable.has_value() && *result.error->retryable,
              "OpenAICompatibleAdapter should mark 5xx transport failures as retryable for 040 retry_budget governance");
  assert_true(result.error->details.stage == "llm.openai_compatible.generate",
              "OpenAICompatibleAdapter should emit a stable stage string for transport failure diagnostics");
}

void test_ollama_adapter_maps_chat_request_and_usage_response() {
  auto transport = std::make_shared<CapturingTransport>();
  transport->next_response = LLMTransportResponse{
      .status_code = 200U,
      .body = R"({
        "model":"qwen2.5:14b-instruct-q4_K_M",
        "created_at":"2026-04-13T18:25:00Z",
        "message":{
          "role":"assistant",
          "content":"{\"plan\":\"ready\"}",
          "thinking":"kept-local"
        },
        "done_reason":"stop",
        "done":true,
        "prompt_eval_count":42,
        "eval_count":11
      })",
      .error_message = {},
  };

  OllamaAdapter adapter(transport);
  assert_true(adapter.init(make_ollama_config()),
              "OllamaAdapter should initialize with projected LAN provider config and transport dependency");

  const auto result = adapter.generate(make_ollama_request());

  assert_true(transport->last_request.has_value(),
              "OllamaAdapter should issue a concrete transport request during generate()");
  assert_equal(1, transport->call_count,
               "OllamaAdapter should call transport exactly once for a unary request");
  assert_true(transport->last_request->method == LLMTransportMethod::Post,
              "OllamaAdapter should map unary generate() to POST /api/chat");
  assert_equal(std::string("http://lan-ollama.internal:11434/api/chat"),
               transport->last_request->url,
               "OllamaAdapter should append /api/chat to the projected LAN base_url");
  assert_equal(std::string("profile://llm/providers/lan-ollama/noauth"),
               transport->last_request->auth_ref,
               "OllamaAdapter should preserve auth_ref even when the LAN deployment uses injected local auth policy");
  assert_equal(std::string("lan/ollama-primary"), transport->last_request->base_url_alias,
               "OllamaAdapter should forward base_url_alias for downstream endpoint indirection");
  assert_true(transport->last_request->body.find("\"model\":\"qwen2.5:14b-instruct-q4_K_M\"") != std::string::npos,
              "OllamaAdapter should derive the concrete model id from request.model_route");
  assert_true(transport->last_request->body.find("\"role\":\"system\"") != std::string::npos &&
                  transport->last_request->body.find("\"content\":\"answer in json\"") != std::string::npos,
              "OllamaAdapter should down-map developer prompts into the system role used by Ollama native chat");
  assert_true(transport->last_request->body.find("\"role\":\"user\"") != std::string::npos &&
                  transport->last_request->body.find("\"content\":\"summarize adapter state\"") != std::string::npos,
              "OllamaAdapter should keep end-user content in the Ollama messages array");
  assert_true(transport->last_request->body.find("\"format\":\"json\"") != std::string::npos,
              "OllamaAdapter should map json_object response format into Ollama native json mode");
  assert_true(transport->last_request->body.find("\"options\":{\"num_predict\":96}") != std::string::npos,
              "OllamaAdapter should project max_output_tokens into Ollama num_predict runtime options");

  assert_true(result.response.has_value(),
              "OllamaAdapter should return a shared LLMResponse on successful 2xx provider responses");
  assert_equal(std::string("req-026-001"), *result.response->request_id,
               "OllamaAdapter should preserve request_id in the shared response");
  assert_equal(std::string("call-026-001"), *result.response->llm_call_id,
               "OllamaAdapter should preserve llm_call_id in the shared response");
  assert_equal(std::string("{\"plan\":\"ready\"}"), *result.response->content_payload,
               "OllamaAdapter should map the assistant message content into the shared response payload");
  assert_equal(std::string("qwen2.5:14b-instruct-q4_K_M"), *result.response->model_name,
               "OllamaAdapter should surface the concrete LAN model identity in the shared response");
  assert_equal(std::string("stop"), *result.response->finish_reason,
               "OllamaAdapter should preserve done_reason for 022 canonicalization");
  assert_true(result.usage.has_value() && result.usage->prompt_tokens == 42U &&
                  result.usage->completion_tokens == 11U && result.usage->total_tokens == 53U,
              "OllamaAdapter should derive usage from prompt_eval_count and eval_count for 023 aggregation");
  assert_equal(std::string("kept-local"), result.provider_diagnostics.reasoning_content,
               "OllamaAdapter should keep thinking output in adapter diagnostics instead of shared contracts");
}

void test_ollama_adapter_surfaces_transport_failures_without_throwing() {
  auto transport = std::make_shared<CapturingTransport>();
  transport->next_response = LLMTransportResponse{
      .status_code = 503U,
      .body = {},
      .error_message = {},
  };

  OllamaAdapter adapter(transport);
  assert_true(adapter.init(make_ollama_config()),
              "OllamaAdapter should initialize before transport failure coverage");

  const auto result = adapter.generate(make_ollama_request());

  assert_true(!result.response.has_value() && result.error.has_value() && result.result_code.has_value(),
              "OllamaAdapter should surface transport failures through AdapterCallResult error fields instead of exceptions");
  assert_true(*result.result_code == ResultCode::ProviderTimeout,
              "OllamaAdapter should map retryable HTTP transport failures to ProviderTimeout in the current shared code set");
  assert_true(result.error->retryable.has_value() && *result.error->retryable,
              "OllamaAdapter should mark 5xx transport failures as retryable for LAN retry_budget governance");
  assert_true(result.error->details.stage == "llm.ollama_native.generate",
              "OllamaAdapter should emit a stable stage string for transport failure diagnostics");
}

}  // namespace

int main() {
  try {
    test_openai_compatible_adapter_maps_unary_request_and_success_response();
    test_openai_compatible_adapter_surfaces_transport_failures_without_throwing();
    test_ollama_adapter_maps_chat_request_and_usage_response();
    test_ollama_adapter_surfaces_transport_failures_without_throwing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
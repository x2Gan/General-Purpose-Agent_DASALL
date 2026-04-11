#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "MockLLMAdapter.h"
#include "support/TestAssertions.h"

namespace {

dasall::contracts::LLMRequest make_request(std::string prompt) {
  dasall::contracts::LLMRequest request;
  request.request_id = "req-llm-mock-001";
  request.llm_call_id = "call-llm-mock-001";
  request.model_route = "deepseek-chat";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{std::move(prompt)};
  request.prompt_id = "prompt.test.mock";
  request.prompt_version = "2026-04-11.1";
  return request;
}

dasall::llm::LLMAdapterConfig make_config() {
  dasall::llm::LLMAdapterConfig config;
  config.adapter_id = "mock-llm";
  config.adapter_family = "test_double";
  config.base_url = "mock://llm";
  config.auth_ref = "secret://llm/mock";
  config.header_refs = {"profile://headers/x-trace-id"};
  config.timeout_ms = 2500;
  config.max_retries = 2;
  config.capability_tags = {"unary", "stream", "health"};
  return config;
}

dasall::contracts::ErrorInfo make_provider_timeout_error() {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::ResultCodeCategory::Provider;
  error.retryable = true;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout);
  error.details.message = "mock provider timeout";
  error.details.stage = "generate";
  error.source_ref.ref_type = "adapter";
  error.source_ref.ref_id = "mock-llm";
  return error;
}

dasall::llm::AdapterCallResult make_failure_result() {
  dasall::llm::AdapterCallResult result;
  result.error = make_provider_timeout_error();
  result.result_code = dasall::contracts::ResultCode::ProviderTimeout;
  return result;
}

void test_mock_llm_adapter_supports_programmable_generate_results_and_call_counts() {
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MockLLMAdapter adapter;
  adapter.set_generate_handler([](const dasall::contracts::LLMRequest& request) {
    dasall::contracts::LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
    response.content_payload = "handled:" + request.messages->front();

    dasall::llm::AdapterCallResult result;
    result.response = std::move(response);
    return result;
  });
  adapter.set_stream_session(dasall::llm::StreamSessionRef{.session_id = "stream-session-001"});

  assert_true(adapter.init(make_config()),
              "MockLLMAdapter should default init() to success for fixture setup reuse");

  const auto request = make_request("ping");
  const auto generate_result = adapter.generate(request);
  const auto stream_ref = adapter.stream_generate(request, nullptr);

  assert_true(generate_result.has_consistent_values(),
              "MockLLMAdapter should preserve AdapterCallResult success invariants");
  assert_true(generate_result.response.has_value(),
              "MockLLMAdapter should expose a programmed success response through generate()");
  assert_true(generate_result.response->content_payload.has_value(),
              "MockLLMAdapter success response should preserve content_payload");
  assert_equal("handled:ping", *generate_result.response->content_payload,
               "MockLLMAdapter generate() should return the programmed response payload");
  assert_equal(1, adapter.init_call_count(),
               "MockLLMAdapter should count init() calls for fixture assertions");
  assert_equal(1, adapter.generate_call_count(),
               "MockLLMAdapter should count generate() calls for unary path assertions");
  assert_equal(1, adapter.stream_generate_call_count(),
               "MockLLMAdapter should count stream_generate() calls for streaming path assertions");
  assert_true(adapter.last_init_config().has_value(),
              "MockLLMAdapter should retain the last init config for verification");
  assert_true(adapter.last_request().has_value(),
              "MockLLMAdapter should retain the last unary request for verification");
  assert_equal("ping", adapter.last_prompt(),
               "MockLLMAdapter should keep the last prompt string for legacy and unary verification");
  assert_equal("stream-session-001", stream_ref.session_id,
               "MockLLMAdapter should return the programmed stream session reference");
}

void test_mock_llm_adapter_supports_programmable_failures_and_health_checks() {
  using dasall::llm::HealthStatus;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MockLLMAdapter adapter;
  adapter.set_init_result(false);
  adapter.set_generate_result(make_failure_result());
  adapter.set_health_status(HealthStatus{
      .ready = false,
      .degraded = true,
      .message = "mock adapter degraded",
  });

  assert_true(!adapter.init(make_config()),
              "MockLLMAdapter should allow tests to force init() failure");

  const auto result = adapter.generate(make_request("timeout"));
  const auto health = adapter.health_check();

  assert_true(result.has_consistent_values(),
              "MockLLMAdapter should preserve AdapterCallResult failure invariants");
  assert_true(!result.response.has_value(),
              "MockLLMAdapter failure result should not fabricate a response payload");
  assert_true(result.error.has_value(),
              "MockLLMAdapter should surface programmed ErrorInfo on failure");
  assert_true(result.result_code.has_value(),
              "MockLLMAdapter should surface programmed ResultCode on failure");
  assert_true(health.degraded,
              "MockLLMAdapter health_check() should preserve programmed degraded state");
  assert_true(!health.is_healthy(),
              "MockLLMAdapter health_check() should report unhealthy when ready=false or degraded=true");
  assert_equal("mock adapter degraded", health.message,
               "MockLLMAdapter health_check() should preserve the programmed message");
  assert_equal(1, adapter.health_check_call_count(),
               "MockLLMAdapter should count health_check() calls for fixture assertions");
}

void test_mock_llm_adapter_preserves_legacy_string_stub_helpers() {
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;

  MockLLMAdapter adapter;
  adapter.set_handler([](const std::string& prompt) {
    return "legacy:" + prompt;
  });

  const std::string result = adapter.invoke("ping");

  assert_equal("legacy:ping", result,
               "MockLLMAdapter should preserve the legacy invoke() helper for existing smoke tests");
  assert_equal(1, adapter.call_count(),
               "MockLLMAdapter invoke() should still increment the generate call counter");
  assert_equal("ping", adapter.last_prompt(),
               "MockLLMAdapter invoke() should still preserve the last prompt string");
}

}  // namespace

int main() {
  try {
    test_mock_llm_adapter_supports_programmable_generate_results_and_call_counts();
    test_mock_llm_adapter_supports_programmable_failures_and_health_checks();
    test_mock_llm_adapter_preserves_legacy_string_stub_helpers();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
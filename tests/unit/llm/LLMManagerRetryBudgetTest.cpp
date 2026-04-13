#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/LLMManager.h"

#include "../../mocks/include/MockLLMAdapter.h"

#include "ModelRouterTestSupport.h"

namespace {

dasall::llm::route::AdapterRegistration make_registration(
    std::string provider_id,
    std::string model_id,
    std::string adapter_id,
    std::shared_ptr<dasall::tests::mocks::MockLLMAdapter> adapter) {
  return dasall::llm::route::AdapterRegistration{
      .provider_id = std::move(provider_id),
      .model_id = std::move(model_id),
      .adapter_id = std::move(adapter_id),
      .deployment_type = "cloud",
      .capability_tags = {"cloud", "unary"},
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

dasall::contracts::LLMRequest make_request() {
  dasall::contracts::LLMRequest request;
  request.request_id = "req-llm-retry-001";
  request.llm_call_id = "call-llm-retry-001";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"retry"};
  request.prompt_id = "prompt.test.retry";
  request.prompt_version = "2026-04-13.1";
  return request;
}

dasall::llm::LLMSubsystemConfig make_execution_config(std::uint32_t retry_budget,
                                                      std::uint32_t worker_threads = 1U) {
  auto config = dasall::llm::test_support::make_config("planner",
                                                       "deepseek-prod/deepseek-chat");
  config.timeout_policy.timeout_ms = 100U;
  config.timeout_policy.retry_budget = retry_budget;
  config.worker_threads = worker_threads;
  return config;
}

dasall::llm::AdapterCallResult make_success_result(std::string content) {
  dasall::contracts::LLMResponse response;
  response.request_id = "req-llm-retry-001";
  response.llm_call_id = "call-llm-retry-001";
  response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
  response.content_payload = std::move(content);

  dasall::llm::AdapterCallResult result;
  result.response = std::move(response);
  return result;
}

dasall::llm::AdapterCallResult make_retryable_failure(std::string message) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::ResultCodeCategory::Provider;
  error.retryable = true;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout);
  error.details.message = std::move(message);
  error.details.stage = "mock.adapter.generate";
  error.source_ref.ref_type = "adapter";
  error.source_ref.ref_id = "deepseek-cloud";

  dasall::llm::AdapterCallResult result;
  result.error = std::move(error);
  result.result_code = dasall::contracts::ResultCode::ProviderTimeout;
  return result;
}

void test_retry_budget_retries_same_route_until_success_within_budget() {
  using dasall::llm::LLMCallExecutor;
  using dasall::llm::route::AdapterRegistry;
  using dasall::llm::route::AdapterRegistryConfig;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 4U}),
              "AdapterRegistry should initialize with a generous block threshold for retry success coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  int attempt_counter = 0;
  adapter->set_generate_handler([&attempt_counter](const dasall::contracts::LLMRequest&) {
    ++attempt_counter;
    if (attempt_counter < 3) {
      return make_retryable_failure("transient timeout");
    }

    return make_success_result("retry-success");
  });
  assert_true(registry.register_adapter(
                  make_registration("deepseek-prod",
                                    "deepseek-chat",
                                    "deepseek-cloud",
                                    adapter)),
              "AdapterRegistry should register the route used by retry success coverage");

  LLMCallExecutor executor;
  assert_true(executor.init(make_execution_config(2U)),
              "LLMCallExecutor should initialize for retry success coverage");

  const auto result = executor.execute_unary(
      "deepseek-prod/deepseek-chat",
      make_request(),
      registry);

  assert_true(result.has_consistent_values() && result.succeeded(),
              "LLMCallExecutor should keep retrying the same route until a retryable attempt succeeds within budget");
  assert_equal(3, static_cast<int>(result.attempts_started),
               "LLMCallExecutor should spend retry_budget + 1 total attempts on a successful same-route retry sequence");
  assert_equal(3, adapter->generate_call_count(),
               "LLMCallExecutor should invoke the adapter exactly once per consumed retry attempt");
  assert_true(!registry.health_snapshot().route_is_blocked("deepseek-prod", "deepseek-chat"),
              "LLMCallExecutor should clear prior transient failures after a later success on the same route");
  assert_equal(0,
               static_cast<int>(registry.health_snapshot().consecutive_failures_for(
                   "deepseek-prod", "deepseek-chat")),
               "LLMCallExecutor should reset AdapterRegistry failure counters after a successful retry");
}

void test_retry_budget_stops_when_route_becomes_blocked() {
  using dasall::llm::LLMCallExecutionFailureReason;
  using dasall::llm::LLMCallExecutor;
  using dasall::llm::route::AdapterRegistry;
  using dasall::llm::route::AdapterRegistryConfig;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 2U}),
              "AdapterRegistry should initialize with a strict block threshold for retry exhaustion coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler([](const dasall::contracts::LLMRequest&) {
    return make_retryable_failure("persistent timeout");
  });
  assert_true(registry.register_adapter(
                  make_registration("deepseek-prod",
                                    "deepseek-chat",
                                    "deepseek-cloud",
                                    adapter)),
              "AdapterRegistry should register the route used by retry block coverage");

  LLMCallExecutor executor;
  assert_true(executor.init(make_execution_config(3U)),
              "LLMCallExecutor should initialize for retry block coverage");

  const auto result = executor.execute_unary(
      "deepseek-prod/deepseek-chat",
      make_request(),
      registry);

  assert_true(result.has_consistent_values() && !result.succeeded(),
              "LLMCallExecutor should fail once the route becomes blocked before retry budget is fully consumed");
  assert_true(result.failure_reason.has_value() &&
                  *result.failure_reason == LLMCallExecutionFailureReason::RouteBlocked,
              "LLMCallExecutor should surface a blocked route when AdapterRegistry trips the breaker threshold");
  assert_equal(2, static_cast<int>(result.attempts_started),
               "LLMCallExecutor should stop same-route retries as soon as the blocked threshold is reached");
  assert_equal(2, adapter->generate_call_count(),
               "LLMCallExecutor should not call the adapter again after the route becomes blocked");
  assert_true(registry.health_snapshot().route_is_blocked("deepseek-prod", "deepseek-chat"),
              "LLMCallExecutor should leave the route blocked in AdapterRegistry once the failure threshold is crossed");
}

}  // namespace

int main() {
  try {
    test_retry_budget_retries_same_route_until_success_within_budget();
    test_retry_budget_stops_when_route_becomes_blocked();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <atomic>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
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
  request.request_id = "req-llm-concurrency-001";
  request.llm_call_id = "call-llm-concurrency-001";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"concurrency"};
  request.prompt_id = "prompt.test.concurrency";
  request.prompt_version = "2026-04-13.1";
  return request;
}

dasall::llm::LLMSubsystemConfig make_execution_config() {
  auto config = dasall::llm::test_support::make_config("planner",
                                                       "deepseek-prod/deepseek-chat");
  config.timeout_policy.timeout_ms = 100U;
  config.timeout_policy.retry_budget = 0U;
  config.worker_threads = 1U;
  return config;
}

dasall::llm::AdapterCallResult make_success_result(std::string content) {
  dasall::contracts::LLMResponse response;
  response.request_id = "req-llm-concurrency-001";
  response.llm_call_id = "call-llm-concurrency-001";
  response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
  response.content_payload = std::move(content);

  dasall::llm::AdapterCallResult result;
  result.response = std::move(response);
  return result;
}

void test_concurrency_guard_rejects_when_inflight_limit_is_exhausted() {
  using dasall::contracts::ResultCode;
  using dasall::llm::LLMCallExecutionFailureReason;
  using dasall::llm::LLMCallExecutionResult;
  using dasall::llm::LLMCallExecutor;
  using dasall::llm::route::AdapterRegistry;
  using dasall::llm::route::AdapterRegistryConfig;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "AdapterRegistry should initialize for concurrency-guard coverage");

  std::promise<void> first_call_started_promise;
  auto first_call_started = first_call_started_promise.get_future();
  std::promise<void> release_first_call_promise;
  auto release_first_call = release_first_call_promise.get_future().share();
  std::atomic<bool> first_call_signaled{false};

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler(
      [&first_call_started_promise, release_first_call, &first_call_signaled](
          const dasall::contracts::LLMRequest&) {
        bool expected = false;
        if (first_call_signaled.compare_exchange_strong(expected, true)) {
          first_call_started_promise.set_value();
        }
        release_first_call.wait();
        return make_success_result("concurrency-ok");
      });
  assert_true(registry.register_adapter(
                  make_registration("deepseek-prod",
                                    "deepseek-chat",
                                    "deepseek-cloud",
                                    adapter)),
              "AdapterRegistry should register the route used by concurrency coverage");

  LLMCallExecutor executor;
  assert_true(executor.init(make_execution_config()),
              "LLMCallExecutor should initialize for concurrency coverage");

  std::promise<LLMCallExecutionResult> first_result_promise;
  auto first_result_future = first_result_promise.get_future();
  std::thread first_thread([&]() {
    first_result_promise.set_value(executor.execute_unary(
        "deepseek-prod/deepseek-chat", make_request(), registry));
  });

  first_call_started.wait();
  assert_equal(1, static_cast<int>(executor.active_call_count()),
               "LLMCallExecutor should count the inflight unary call while the adapter is still running");

  const auto second_result = executor.execute_unary(
      "deepseek-prod/deepseek-chat", make_request(), registry);

  assert_true(second_result.has_consistent_values() && !second_result.succeeded(),
              "LLMCallExecutor should fail fast when the bounded inflight limit is exhausted");
  assert_true(second_result.failure_reason.has_value() &&
                  *second_result.failure_reason == LLMCallExecutionFailureReason::ConcurrencyRejected,
              "LLMCallExecutor should classify inflight saturation as ConcurrencyRejected");
  assert_true(second_result.result_code.has_value() &&
                  *second_result.result_code == ResultCode::RuntimeRetryExhausted,
              "LLMCallExecutor should surface concurrency saturation through a runtime failure code");
  assert_equal(1, adapter->generate_call_count(),
               "LLMCallExecutor should not enter the adapter again when the inflight limit already holds the only slot");

  release_first_call_promise.set_value();
  first_thread.join();
  const auto first_result = first_result_future.get();

  assert_true(first_result.has_consistent_values() && first_result.succeeded(),
              "LLMCallExecutor should still let the original inflight call complete successfully");
  assert_equal(0, static_cast<int>(executor.active_call_count()),
               "LLMCallExecutor should release the inflight slot after the running call completes");
}

}  // namespace

int main() {
  try {
    test_concurrency_guard_rejects_when_inflight_limit_is_exhausted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
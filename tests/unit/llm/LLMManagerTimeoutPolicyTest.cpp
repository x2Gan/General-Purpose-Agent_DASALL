#include <chrono>
#include <exception>
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

dasall::contracts::LLMRequest make_request(
    std::vector<std::string> messages,
    std::optional<std::uint32_t> timeout_ms = std::nullopt) {
  dasall::contracts::LLMRequest request;
  request.request_id = "req-llm-timeout-001";
  request.llm_call_id = "call-llm-timeout-001";
  request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  request.messages = std::move(messages);
  request.prompt_id = "prompt.test.timeout";
  request.prompt_version = "2026-04-13.1";
  request.timeout_ms = timeout_ms;
  return request;
}

dasall::llm::LLMSubsystemConfig make_execution_config(std::int64_t timeout_ms,
                                                      std::uint32_t retry_budget,
                                                      std::uint32_t worker_threads) {
  auto config = dasall::llm::test_support::make_config("planner",
                                                       "deepseek-prod/deepseek-chat");
  config.timeout_policy.timeout_ms = timeout_ms;
  config.timeout_policy.retry_budget = retry_budget;
  config.worker_threads = worker_threads;
  return config;
}

dasall::llm::AdapterCallResult make_success_result(std::string content) {
  dasall::contracts::LLMResponse response;
  response.request_id = "req-llm-timeout-001";
  response.llm_call_id = "call-llm-timeout-001";
  response.response_kind = dasall::contracts::LLMResponseKind::DirectResponse;
  response.content_payload = std::move(content);

  dasall::llm::AdapterCallResult result;
  result.response = std::move(response);
  return result;
}

void test_timeout_policy_clamps_request_deadline_and_accepts_fast_success() {
  using dasall::llm::LLMCallExecutor;
  using dasall::llm::route::AdapterRegistry;
  using dasall::llm::route::AdapterRegistryConfig;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "AdapterRegistry should initialize for timeout-policy success coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler([](const dasall::contracts::LLMRequest& request) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return make_success_result(request.messages->front() + ":ok");
  });
  assert_true(registry.register_adapter(
                  make_registration("deepseek-prod",
                                    "deepseek-chat",
                                    "deepseek-cloud",
                                    adapter)),
              "AdapterRegistry should register the route used by timeout success coverage");

  LLMCallExecutor executor;
  assert_true(executor.init(make_execution_config(50, 0U, 1U)),
              "LLMCallExecutor should initialize from a valid llm config");

  const auto result = executor.execute_unary(
      "deepseek-prod/deepseek-chat",
      make_request({"ping"}, 15U),
      registry);

  assert_true(result.has_consistent_values() && result.succeeded(),
              "LLMCallExecutor should preserve a fast success inside timeout policy");
  assert_equal(1, static_cast<int>(result.attempts_started),
               "LLMCallExecutor should complete the fast success in one attempt");
  assert_true(adapter->last_request().has_value() &&
                  adapter->last_request()->timeout_ms.has_value(),
              "LLMCallExecutor should forward an effective timeout to the adapter request");
  assert_equal(15, static_cast<int>(*adapter->last_request()->timeout_ms),
               "LLMCallExecutor should clamp the adapter timeout to the stricter per-request deadline");
  assert_true(adapter->last_request()->model_route.has_value() &&
                  *adapter->last_request()->model_route == "deepseek-prod/deepseek-chat",
              "LLMCallExecutor should replace pre-route hints with the resolved concrete route key");
}

void test_timeout_policy_rejects_late_successes_and_records_timeout_failure() {
  using dasall::contracts::ResultCode;
  using dasall::llm::LLMCallExecutionFailureReason;
  using dasall::llm::LLMCallExecutor;
  using dasall::llm::route::AdapterRegistry;
  using dasall::llm::route::AdapterRegistryConfig;
  using dasall::tests::mocks::MockLLMAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AdapterRegistry registry;
  assert_true(registry.init(AdapterRegistryConfig{.blocked_failure_threshold = 3U}),
              "AdapterRegistry should initialize for timeout-policy failure coverage");

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler([](const dasall::contracts::LLMRequest&) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return make_success_result("late-success");
  });
  assert_true(registry.register_adapter(
                  make_registration("deepseek-prod",
                                    "deepseek-chat",
                                    "deepseek-cloud",
                                    adapter)),
              "AdapterRegistry should register the route used by timeout failure coverage");

  LLMCallExecutor executor;
  assert_true(executor.init(make_execution_config(5, 0U, 1U)),
              "LLMCallExecutor should initialize for timeout failure coverage");

  const auto result = executor.execute_unary(
      "deepseek-prod/deepseek-chat",
      make_request({"late"}),
      registry);

  assert_true(result.has_consistent_values() && !result.succeeded(),
              "LLMCallExecutor should convert a late adapter response into a timeout failure");
  assert_true(result.failure_reason.has_value() &&
                  *result.failure_reason == LLMCallExecutionFailureReason::Timeout,
              "LLMCallExecutor should classify an elapsed-budget violation as Timeout");
  assert_true(result.result_code.has_value() &&
                  *result.result_code == ResultCode::ProviderTimeout,
              "LLMCallExecutor should surface timeout failures through ProviderTimeout");
  assert_true(result.error.has_value() &&
                  result.error->retryable.value_or(false),
              "LLMCallExecutor should keep timeout failures retryable for later fallback owners");
  assert_equal(1,
               static_cast<int>(registry.health_snapshot().consecutive_failures_for(
                   "deepseek-prod", "deepseek-chat")),
               "LLMCallExecutor should record a timeout failure into AdapterRegistry counters");
}

}  // namespace

int main() {
  try {
    test_timeout_policy_clamps_request_deadline_and_accepts_fast_success();
    test_timeout_policy_rejects_late_successes_and_records_timeout_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
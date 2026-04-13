#include "LLMManager.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {

using AdapterCallResult = dasall::llm::AdapterCallResult;
using LLMCallExecutionFailureReason = dasall::llm::LLMCallExecutionFailureReason;
using LLMCallExecutionResult = dasall::llm::LLMCallExecutionResult;
using LLMTimeoutConfig = dasall::llm::LLMTimeoutConfig;
using ResultCode = dasall::contracts::ResultCode;

constexpr std::string_view kExecutionStage = "llm.manager.execute_unary";

std::uint32_t clamp_timeout_ms(std::uint64_t timeout_ms) {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      timeout_ms, std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t effective_timeout_ms(const LLMTimeoutConfig& timeout_policy,
                                   const dasall::contracts::LLMRequest& request) {
  const auto configured_timeout_ms = clamp_timeout_ms(
      static_cast<std::uint64_t>(std::max<std::int64_t>(timeout_policy.timeout_ms, 1)));

  if (!request.timeout_ms.has_value()) {
    return configured_timeout_ms;
  }

  return std::min(configured_timeout_ms, *request.timeout_ms);
}

dasall::contracts::LLMRequest make_attempt_request(
    const dasall::contracts::LLMRequest& request,
    std::string_view route_key,
    std::uint32_t timeout_ms) {
  auto attempt_request = request;
  attempt_request.model_route = std::string(route_key);
  attempt_request.request_mode = dasall::contracts::LLMRequestMode::Unary;
  attempt_request.timeout_ms = timeout_ms;
  return attempt_request;
}

dasall::contracts::ErrorInfo make_error(ResultCode result_code,
                                        std::string message,
                                        bool retryable,
                                        std::string route_key) {
  dasall::contracts::ErrorInfo error;
  error.failure_type = dasall::contracts::classify_result_code(result_code);
  error.retryable = retryable;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(result_code);
  error.details.message = std::move(message);
  error.details.stage = std::string(kExecutionStage);
  error.source_ref.ref_type = "route";
  error.source_ref.ref_id = std::move(route_key);
  return error;
}

std::string adapter_failure_message(const AdapterCallResult& adapter_result,
                                    std::string fallback_message) {
  if (adapter_result.error.has_value() &&
      !adapter_result.error->details.message.empty()) {
    return adapter_result.error->details.message;
  }

  return fallback_message;
}

bool adapter_failure_is_retryable(const AdapterCallResult& adapter_result) {
  return adapter_result.error.has_value() &&
         adapter_result.error->retryable.value_or(false);
}

bool try_acquire_active_call_slot(std::atomic<std::uint32_t>& active_call_count,
                                  std::uint32_t limit) {
  auto current = active_call_count.load(std::memory_order_acquire);
  while (current < limit) {
    if (active_call_count.compare_exchange_weak(
            current, current + 1U,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return true;
    }
  }

  return false;
}

class ActiveCallPermit {
 public:
  explicit ActiveCallPermit(std::atomic<std::uint32_t>& active_call_count)
      : active_call_count_(&active_call_count) {}

  ActiveCallPermit(const ActiveCallPermit&) = delete;
  ActiveCallPermit& operator=(const ActiveCallPermit&) = delete;

  ActiveCallPermit(ActiveCallPermit&& other) noexcept
      : active_call_count_(std::exchange(other.active_call_count_, nullptr)) {}

  ActiveCallPermit& operator=(ActiveCallPermit&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    release();
    active_call_count_ = std::exchange(other.active_call_count_, nullptr);
    return *this;
  }

  ~ActiveCallPermit() {
    release();
  }

 private:
  void release() {
    if (active_call_count_ == nullptr) {
      return;
    }

    active_call_count_->fetch_sub(1U, std::memory_order_acq_rel);
    active_call_count_ = nullptr;
  }

  std::atomic<std::uint32_t>* active_call_count_ = nullptr;
};

LLMCallExecutionResult make_success_result(std::string route_key,
                                           std::uint32_t attempts_started,
                                           AdapterCallResult adapter_result) {
  return LLMCallExecutionResult{
      .route_key = std::move(route_key),
      .attempts_started = attempts_started,
      .adapter_result = std::move(adapter_result),
      .error = std::nullopt,
      .result_code = std::nullopt,
      .failure_reason = std::nullopt,
  };
}

LLMCallExecutionResult make_failure_result(
    std::string route_key,
    std::uint32_t attempts_started,
    std::optional<AdapterCallResult> adapter_result,
    ResultCode result_code,
    std::string message,
    bool retryable,
    LLMCallExecutionFailureReason failure_reason) {
  return LLMCallExecutionResult{
      .route_key = std::move(route_key),
      .attempts_started = attempts_started,
      .adapter_result = std::move(adapter_result),
      .error = make_error(result_code, std::move(message), retryable,
                          route_key),
      .result_code = result_code,
      .failure_reason = failure_reason,
  };
}

AdapterCallResult make_timeout_result(std::string route_key, std::string message) {
  AdapterCallResult result;
  result.error = make_error(ResultCode::ProviderTimeout, std::move(message), true,
                            std::move(route_key));
  result.result_code = ResultCode::ProviderTimeout;
  return result;
}

}  // namespace

namespace dasall::llm {

bool LLMCallExecutionResult::succeeded() const {
  return adapter_result.has_value() && adapter_result->response.has_value();
}

bool LLMCallExecutionResult::has_consistent_values() const {
  if (adapter_result.has_value() && !adapter_result->has_consistent_values()) {
    return false;
  }

  if (succeeded()) {
    return attempts_started > 0U && !error.has_value() && !result_code.has_value() &&
           !failure_reason.has_value() && !route_key.empty();
  }

  if (!failure_reason.has_value() || !error.has_value() || !result_code.has_value()) {
    return false;
  }

  if (adapter_result.has_value() && adapter_result->response.has_value()) {
    return false;
  }

  if (error->failure_type.has_value() &&
      contracts::classify_result_code(*result_code) != *error->failure_type) {
    return false;
  }

  return true;
}

bool LLMCallExecutor::init(const LLMSubsystemConfig& config) {
  if (!config.has_consistent_values()) {
    return false;
  }

  config_ = config;
  active_call_count_.store(0U, std::memory_order_release);
  initialized_ = true;
  return true;
}

LLMCallExecutionResult LLMCallExecutor::execute_unary(
    std::string_view route_key,
    const contracts::LLMRequest& request,
    route::AdapterRegistry& registry) {
  const std::string route_key_string(route_key);
  if (!initialized_) {
    return make_failure_result(route_key_string, 0U, std::nullopt,
                               ResultCode::RuntimeRetryExhausted,
                               "llm call executor is not initialized",
                               false,
                               LLMCallExecutionFailureReason::NotInitialized);
  }

  std::uint32_t attempts_started = 0U;
  for (std::uint32_t attempt = 0U;
       attempt <= config_.timeout_policy.retry_budget;
       ++attempt) {
    const auto route_state = registry.resolve_route(route_key);
    if (!route_state.has_value() || route_state->adapter == nullptr) {
      return make_failure_result(route_key_string, attempts_started, std::nullopt,
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter route is not registered",
                                 false,
                                 LLMCallExecutionFailureReason::RouteUnavailable);
    }

    if (route_state->blocked) {
      return make_failure_result(route_key_string, attempts_started, std::nullopt,
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter route is blocked by circuit threshold",
                                 false,
                                 LLMCallExecutionFailureReason::RouteBlocked);
    }

    if (!try_acquire_active_call_slot(active_call_count_, config_.worker_threads)) {
      return make_failure_result(route_key_string, attempts_started, std::nullopt,
                                 ResultCode::RuntimeRetryExhausted,
                                 "active llm calls limit reached",
                                 false,
                                 LLMCallExecutionFailureReason::ConcurrencyRejected);
    }

    ActiveCallPermit active_call_permit(active_call_count_);
    const auto attempt_request = make_attempt_request(
        request, route_key,
        effective_timeout_ms(config_.timeout_policy, request));

    // The current adapter SPI is synchronous, so 040 enforces timeout by
    // propagating a deadline hint and rejecting responses that return after the
    // configured budget rather than spawning detached worker threads.
    const auto started_at = std::chrono::steady_clock::now();
    auto adapter_result = route_state->adapter->generate(attempt_request);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started_at)
                                .count();
    ++attempts_started;

    if (!adapter_result.has_consistent_values()) {
      registry.record_call_failure(route_key,
                                   "adapter returned inconsistent unary execution result");
      if (const auto updated_route = registry.resolve_route(route_key);
          updated_route.has_value() && updated_route->blocked) {
        return make_failure_result(route_key_string, attempts_started,
                                   std::move(adapter_result),
                                   ResultCode::RuntimeRetryExhausted,
                                   "adapter route blocked after inconsistent execution failures",
                                   false,
                                   LLMCallExecutionFailureReason::RouteBlocked);
      }

      return make_failure_result(route_key_string, attempts_started,
                                 std::move(adapter_result),
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter returned inconsistent unary execution result",
                                 false,
                                 LLMCallExecutionFailureReason::AdapterFailure);
    }

    if (elapsed_ms > static_cast<std::int64_t>(*attempt_request.timeout_ms)) {
      auto timeout_result = make_timeout_result(
          route_key_string,
          "adapter invocation exceeded timeout_policy after " +
              std::to_string(elapsed_ms) + "ms");
      registry.record_call_failure(route_key,
                                   timeout_result.error->details.message);
      if (const auto updated_route = registry.resolve_route(route_key);
          updated_route.has_value() && updated_route->blocked) {
        return make_failure_result(route_key_string, attempts_started,
                                   std::move(timeout_result),
                                   ResultCode::RuntimeRetryExhausted,
                                   "adapter route blocked after consecutive timeout failures",
                                   false,
                                   LLMCallExecutionFailureReason::RouteBlocked);
      }

      if (attempt < config_.timeout_policy.retry_budget) {
        continue;
      }

      return make_failure_result(route_key_string, attempts_started,
                                 std::move(timeout_result),
                                 ResultCode::ProviderTimeout,
                                 "adapter invocation exceeded timeout_policy",
                                 true,
                                 LLMCallExecutionFailureReason::Timeout);
    }

    if (adapter_result.response.has_value()) {
      registry.record_call_success(route_key, "llm unary execution succeeded");
      return make_success_result(route_key_string, attempts_started,
                                 std::move(adapter_result));
    }

    const auto failure_message = adapter_failure_message(
        adapter_result, "adapter unary execution failed");
    registry.record_call_failure(route_key, failure_message);
    if (const auto updated_route = registry.resolve_route(route_key);
        updated_route.has_value() && updated_route->blocked) {
      return make_failure_result(route_key_string, attempts_started,
                                 std::move(adapter_result),
                                 ResultCode::RuntimeRetryExhausted,
                                 "adapter route blocked after consecutive call failures",
                                 false,
                                 LLMCallExecutionFailureReason::RouteBlocked);
    }

    if (adapter_failure_is_retryable(adapter_result) &&
        attempt < config_.timeout_policy.retry_budget) {
      continue;
    }

    return make_failure_result(
        route_key_string,
        attempts_started,
        std::move(adapter_result),
        adapter_result.result_code.value_or(ResultCode::RuntimeRetryExhausted),
        failure_message,
        adapter_failure_is_retryable(adapter_result),
        LLMCallExecutionFailureReason::AdapterFailure);
  }

  return make_failure_result(route_key_string, attempts_started, std::nullopt,
                             ResultCode::RuntimeRetryExhausted,
                             "llm unary execution exhausted retry budget",
                             false,
                             LLMCallExecutionFailureReason::AdapterFailure);
}

std::uint32_t LLMCallExecutor::active_call_count() const {
  return active_call_count_.load(std::memory_order_acquire);
}

bool LLMCallExecutor::is_initialized() const {
  return initialized_;
}

}  // namespace dasall::llm
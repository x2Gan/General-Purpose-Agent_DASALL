#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "LLMSubsystemConfig.h"
#include "adapters/AdapterCallResult.h"
#include "route/AdapterRegistry.h"

namespace dasall::llm {

enum class LLMCallExecutionFailureReason {
  NotInitialized = 0,
  RouteUnavailable = 1,
  RouteBlocked = 2,
  ConcurrencyRejected = 3,
  Timeout = 4,
  AdapterFailure = 5,
};

struct LLMCallExecutionResult {
  std::string route_key;
  std::uint32_t attempts_started = 0U;
  std::optional<AdapterCallResult> adapter_result;
  std::optional<contracts::ErrorInfo> error;
  std::optional<contracts::ResultCode> result_code;
  std::optional<LLMCallExecutionFailureReason> failure_reason;

  [[nodiscard]] bool succeeded() const;
  [[nodiscard]] bool has_consistent_values() const;
};

class LLMCallExecutor {
 public:
  bool init(const LLMSubsystemConfig& config);

  [[nodiscard]] LLMCallExecutionResult execute_unary(std::string_view route_key,
                                                     const contracts::LLMRequest& request,
                                                     route::AdapterRegistry& registry);

  [[nodiscard]] std::uint32_t active_call_count() const;
  [[nodiscard]] bool is_initialized() const;

 private:
  LLMSubsystemConfig config_{};
  std::atomic<std::uint32_t> active_call_count_{0U};
  bool initialized_ = false;
};

}  // namespace dasall::llm
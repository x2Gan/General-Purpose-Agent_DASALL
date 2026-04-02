#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::watchdog {

struct HeartbeatSample;
struct TimeoutDecision;

struct TimeoutHistoryWindow {
  std::vector<std::shared_ptr<const HeartbeatSample>> recent_samples;
  std::vector<std::shared_ptr<const TimeoutDecision>> prior_decisions;

  [[nodiscard]] bool has_bindable_inputs() const {
    return !recent_samples.empty() || !prior_decisions.empty();
  }
};

struct TimeoutPolicyEvaluationResult {
  bool ok = false;
  std::shared_ptr<const TimeoutDecision> decision;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static TimeoutPolicyEvaluationResult success(
      std::shared_ptr<const TimeoutDecision> decision) {
    return TimeoutPolicyEvaluationResult{
        .ok = true,
        .decision = std::move(decision),
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static TimeoutPolicyEvaluationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return TimeoutPolicyEvaluationResult{
        .ok = false,
        .decision = nullptr,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool has_decision() const {
    return ok && decision != nullptr && !result_code.has_value() &&
           !error.has_value();
  }
};

class ITimeoutPolicy {
 public:
  virtual ~ITimeoutPolicy() = default;

  [[nodiscard]] virtual TimeoutPolicyEvaluationResult evaluate(
      std::shared_ptr<const HeartbeatSample> candidate,
      const TimeoutHistoryWindow& history) const = 0;
};

}  // namespace dasall::infra::watchdog
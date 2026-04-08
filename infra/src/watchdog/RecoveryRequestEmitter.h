#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "watchdog/RecoveryHintRequest.h"
#include "watchdog/TimeoutDecision.h"

namespace dasall::infra::watchdog {

struct RecoveryRequestEmissionResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;
  RecoveryHintRequest request;

  [[nodiscard]] static RecoveryRequestEmissionResult success(
      RecoveryHintRequest request) {
    return RecoveryRequestEmissionResult{
        .ok = true,
        .result_code = std::nullopt,
        .error_info = std::nullopt,
        .request = std::move(request),
    };
  }

  [[nodiscard]] static RecoveryRequestEmissionResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return RecoveryRequestEmissionResult{
        .ok = false,
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
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
        .request = {},
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error_info.has_value()) {
      return ok;
    }

    return result_code.has_value() && error_info.has_value() &&
           error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct RecoveryRequestEmitterOptions {
  std::string evidence_ref_prefix = "audit://watchdog/recovery_hint/";
  std::string critical_action = "review_runtime_recovery_for_target";
  std::string fatal_action = "escalate_for_runtime_recovery_review";
};

class RecoveryRequestEmitter {
 public:
  explicit RecoveryRequestEmitter(RecoveryRequestEmitterOptions options = {});

  [[nodiscard]] RecoveryRequestEmissionResult emit_recovery_hint(
      const TimeoutDecision& decision) const;
  [[nodiscard]] std::string sanitize_payload(std::string_view payload) const;

 private:
  [[nodiscard]] std::string suggested_action_for(
      const TimeoutDecision& decision) const;
  [[nodiscard]] std::string build_evidence_ref(
      const TimeoutDecision& decision) const;

  RecoveryRequestEmitterOptions options_;
};

}  // namespace dasall::infra::watchdog
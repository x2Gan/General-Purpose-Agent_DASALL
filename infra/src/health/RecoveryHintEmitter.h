#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "health/HealthStateTypes.h"
#include "health/RecoveryHint.h"

namespace dasall::infra {

struct RecoveryHintEmissionResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;
  RecoveryHint hint;

  [[nodiscard]] static RecoveryHintEmissionResult success(RecoveryHint hint) {
    return RecoveryHintEmissionResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
        .hint = std::move(hint),
    };
  }

  [[nodiscard]] static RecoveryHintEmissionResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return RecoveryHintEmissionResult{
        .ok = false,
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
                .ref_type = "infra.health",
                .ref_id = std::move(source_ref),
            },
        },
        .hint = {},
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!result_code.has_value() && !error.has_value()) {
      return ok;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct RecoveryHintEmitterOptions {
  std::string evidence_ref_prefix = "audit://health/recovery_hint/";
  std::string degraded_action = "observe_and_retry_later";
  std::string unhealthy_action = "escalate_for_runtime_recovery_review";
};

class RecoveryHintEmitter {
 public:
  explicit RecoveryHintEmitter(RecoveryHintEmitterOptions options = {});

  [[nodiscard]] RecoveryHintEmissionResult emit_hint(const HealthSnapshot& snapshot,
                                                     std::string_view reason) const;
  [[nodiscard]] std::string sanitize_hint_payload(std::string_view payload) const;

 private:
  [[nodiscard]] contracts::ResultCode reason_code_for(const HealthSnapshot& snapshot) const;
  [[nodiscard]] RecoveryHintSeverity severity_for(const HealthSnapshot& snapshot) const;
  [[nodiscard]] std::string suggested_action_for(const HealthSnapshot& snapshot) const;
  [[nodiscard]] std::string build_evidence_ref(const HealthSnapshot& snapshot,
                                               std::string_view sanitized_reason) const;
  [[nodiscard]] std::string build_component_segment(const HealthSnapshot& snapshot) const;

  RecoveryHintEmitterOptions options_;
};

}  // namespace dasall::infra
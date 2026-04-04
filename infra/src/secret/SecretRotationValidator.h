#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "secret/SecretTypes.h"

namespace dasall::infra::secret {

struct SecretRotationValidationContext {
  std::string secret_name;
  std::string previous_version;
  std::string candidate_version;
  RotationStrategy strategy = RotationStrategy::Unspecified;
  std::string requested_by;
  std::string reason_code;
  bool validation_required = true;
  bool dual_slot_enabled = true;
  std::uint64_t next_rotation_epoch = 0;
  std::int64_t grace_period_sec = 0;

  [[nodiscard]] bool is_valid() const {
    return !secret_name.empty() && !previous_version.empty() && !candidate_version.empty() &&
           previous_version != candidate_version &&
           strategy != RotationStrategy::Unspecified && !requested_by.empty() &&
           !reason_code.empty() && next_rotation_epoch > 0 && grace_period_sec >= 0;
  }
};

struct SecretRotationValidationDecision {
  bool accepted = false;
  std::string reason_code;
  std::string evidence_ref;
  bool rollback_ready = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretRotationValidationDecision success(
      std::string reason_code,
      std::string evidence_ref,
      bool rollback_ready = false) {
    return SecretRotationValidationDecision{
        .accepted = true,
        .reason_code = std::move(reason_code),
        .evidence_ref = std::move(evidence_ref),
        .rollback_ready = rollback_ready,
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretRotationValidationDecision failure(
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string evidence_ref,
      std::string message,
      std::string stage,
      std::string source_ref,
      bool rollback_ready = false) {
    return SecretRotationValidationDecision{
        .accepted = false,
        .reason_code = std::move(reason_code),
        .evidence_ref = std::move(evidence_ref),
        .rollback_ready = rollback_ready,
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return accepted && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (reason_code.empty() || evidence_ref.empty()) {
      return false;
    }

    if (accepted) {
      return !result_code.has_value() && !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

class ISecretRotationValidator {
 public:
  virtual ~ISecretRotationValidator() = default;

  [[nodiscard]] virtual SecretRotationValidationDecision validate_candidate(
      const SecretRotationValidationContext& context) = 0;
};

struct SecretRotationValidatorOptions {
  std::string evidence_prefix = "audit://secret/rotation/validation/";
};

class AllowAllSecretRotationValidator final : public ISecretRotationValidator {
 public:
  explicit AllowAllSecretRotationValidator(SecretRotationValidatorOptions options = {})
      : options_(std::move(options)) {}

  [[nodiscard]] SecretRotationValidationDecision validate_candidate(
      const SecretRotationValidationContext& context) override {
    if (!context.is_valid()) {
      return SecretRotationValidationDecision::failure(
          contracts::ResultCode::ValidationFieldMissing,
          "validation_context_invalid",
          options_.evidence_prefix + "invalid_context",
          "secret rotation validator requires a valid validation context",
          "secret.rotate.validate",
          "AllowAllSecretRotationValidator",
          false);
    }

    return SecretRotationValidationDecision::success(
        "validation_passed",
        options_.evidence_prefix + context.secret_name + "/" + context.candidate_version,
        context.strategy == RotationStrategy::DualSlot);
  }

 private:
  SecretRotationValidatorOptions options_;
};

}  // namespace dasall::infra::secret
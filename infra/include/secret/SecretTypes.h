#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::secret {

struct SecureBuffer;

enum class SecretAccessMode {
  Unspecified = 0,
  MetadataOnly = 1,
  Materialize = 2,
  Rotate = 3,
  Revoke = 4,
};

enum class SecretClassification {
  Unspecified = 0,
  Credential = 1,
  Token = 2,
  Certificate = 3,
  SensitiveConfig = 4,
};

enum class SecretBackendType {
  Unspecified = 0,
  File = 1,
  Kms = 2,
  Mock = 3,
};

enum class SecretLeaseState {
  Unspecified = 0,
  Active = 1,
  Released = 2,
  Expired = 3,
  Revoked = 4,
  Stale = 5,
};

enum class RotationStrategy {
  Unspecified = 0,
  InPlace = 1,
  DualSlot = 2,
};

enum class RotationValidationState {
  Unspecified = 0,
  Pending = 1,
  Succeeded = 2,
  Failed = 3,
  RolledBack = 4,
};

enum class SecretAuditAction {
  Unspecified = 0,
  AccessGranted = 1,
  AccessDenied = 2,
  Materialized = 3,
  Rotated = 4,
  Revoked = 5,
  Fallback = 6,
  ExpiredAccess = 7,
};

namespace detail {

[[nodiscard]] inline bool has_non_empty_value(const std::optional<std::string>& value) {
  return value.has_value() && !value->empty();
}

[[nodiscard]] inline contracts::ErrorInfo make_error_info(contracts::ResultCode result_code,
                                                          std::string message,
                                                          std::string stage,
                                                          std::string source_ref) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(result_code),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(result_code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "infra.secret",
          .ref_id = std::move(source_ref),
      },
  };
}

}  // namespace detail

struct SecretQuery {
  std::string secret_name;
  std::string version_hint;
  std::string purpose;
  SecretAccessMode access_mode = SecretAccessMode::Unspecified;

  [[nodiscard]] bool is_valid() const {
    return !secret_name.empty() && !purpose.empty() &&
           access_mode != SecretAccessMode::Unspecified;
  }
};

struct SecretAccessContext {
  std::optional<std::string> request_id;
  std::optional<std::string> session_id;
  std::optional<std::string> task_id;
  std::string actor;
  std::string consumer_module;
  std::string permission_domain;

  [[nodiscard]] bool has_audit_anchor() const {
    return detail::has_non_empty_value(request_id) || detail::has_non_empty_value(task_id);
  }

  [[nodiscard]] bool is_valid() const {
    return has_audit_anchor() && !actor.empty() && !consumer_module.empty() &&
           !permission_domain.empty() &&
           (!request_id.has_value() || !request_id->empty()) &&
           (!session_id.has_value() || !session_id->empty()) &&
           (!task_id.has_value() || !task_id->empty());
  }
};

struct SecretDescriptor {
  std::string secret_name;
  SecretBackendType backend_type = SecretBackendType::Unspecified;
  SecretClassification classification = SecretClassification::Unspecified;
  std::string rotation_policy_ref;
  std::string owner_ref;

  [[nodiscard]] bool is_valid() const {
    return !secret_name.empty() && backend_type != SecretBackendType::Unspecified &&
           classification != SecretClassification::Unspecified &&
           !rotation_policy_ref.empty() && !owner_ref.empty();
  }
};

struct SecretHandle {
  std::string handle_id;
  std::string secret_name;
  std::string version;
  std::string backend_ref;
  std::int64_t issued_at_ms = 0;
  std::int64_t expires_at_ms = 0;
  std::string redaction_hint;

  [[nodiscard]] bool is_valid() const {
    return !handle_id.empty() && !secret_name.empty() && !version.empty() &&
           !backend_ref.empty() && issued_at_ms > 0 && expires_at_ms >= issued_at_ms &&
           !redaction_hint.empty();
  }
};

struct SecretLease {
  std::string lease_id;
  std::string handle_id;
  std::string consumer_ref;
  std::int64_t expires_at_ms = 0;
  std::uint64_t rotation_epoch = 0;
  SecretLeaseState state = SecretLeaseState::Unspecified;

  [[nodiscard]] bool is_active() const {
    return state == SecretLeaseState::Active;
  }

  [[nodiscard]] bool is_valid() const {
    return !lease_id.empty() && !handle_id.empty() && !consumer_ref.empty() &&
           expires_at_ms > 0 && rotation_epoch > 0 &&
           state != SecretLeaseState::Unspecified;
  }
};

struct RotationRequest {
  std::string secret_name;
  std::string requested_by;
  std::string reason_code;
  RotationStrategy strategy = RotationStrategy::Unspecified;
  bool validate_only = false;

  [[nodiscard]] bool is_valid() const {
    return !secret_name.empty() && !requested_by.empty() && !reason_code.empty() &&
           strategy != RotationStrategy::Unspecified;
  }
};

struct RotationResult {
  bool rotated = false;
  std::string secret_name;
  std::string previous_version;
  std::string current_version;
  RotationValidationState validation_state = RotationValidationState::Unspecified;
  bool rollback_ready = false;
  std::string evidence_ref;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static RotationResult success(std::string secret_name,
                                              std::string previous_version,
                                              std::string current_version,
                                              std::string evidence_ref,
                                              bool rollback_ready = true) {
    return RotationResult{
        .rotated = true,
        .secret_name = std::move(secret_name),
        .previous_version = std::move(previous_version),
        .current_version = std::move(current_version),
        .validation_state = RotationValidationState::Succeeded,
        .rollback_ready = rollback_ready,
        .evidence_ref = std::move(evidence_ref),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static RotationResult failure(std::string secret_name,
                                              std::string previous_version,
                                              std::string current_version,
                                              std::string evidence_ref,
                                              contracts::ResultCode result_code,
                                              std::string message,
                                              std::string stage,
                                              std::string source_ref,
                                              bool rollback_ready = false) {
    return RotationResult{
        .rotated = false,
        .secret_name = std::move(secret_name),
        .previous_version = std::move(previous_version),
        .current_version = std::move(current_version),
        .validation_state = RotationValidationState::Failed,
        .rollback_ready = rollback_ready,
        .evidence_ref = std::move(evidence_ref),
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (secret_name.empty() || validation_state == RotationValidationState::Unspecified ||
        evidence_ref.empty()) {
      return false;
    }

    if (rotated) {
      return !previous_version.empty() && !current_version.empty() &&
             previous_version != current_version && !result_code.has_value() &&
             !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct SecretAuditEvent {
  std::string actor;
  SecretAuditAction action = SecretAuditAction::Unspecified;
  std::string target_secret;
  std::string consumer_module;
  bool outcome = false;
  std::string reason_code;
  std::string version;
  std::string evidence_ref;
  std::optional<std::string> request_id;
  std::optional<std::string> task_id;

  [[nodiscard]] bool is_valid() const {
    return !actor.empty() && action != SecretAuditAction::Unspecified &&
           !target_secret.empty() && !consumer_module.empty() && !reason_code.empty() &&
           !version.empty() && !evidence_ref.empty() &&
           (detail::has_non_empty_value(request_id) || detail::has_non_empty_value(task_id));
  }
};

struct SecretHandleResult {
  bool ok = false;
  SecretHandle handle;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretHandleResult success(SecretHandle handle) {
    return SecretHandleResult{
        .ok = true,
        .handle = std::move(handle),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretHandleResult failure(contracts::ResultCode result_code,
                                                  std::string message,
                                                  std::string stage,
                                                  std::string source_ref) {
    return SecretHandleResult{
        .ok = false,
        .handle = {},
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (ok) {
      return handle.is_valid() && !result_code.has_value() && !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct SecretMaterializationResult {
  bool ok = false;
  std::shared_ptr<SecureBuffer> materialized_secret;
  SecretLease lease;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretMaterializationResult success(std::shared_ptr<SecureBuffer> materialized_secret,
                                                           SecretLease lease) {
    return SecretMaterializationResult{
        .ok = true,
        .materialized_secret = std::move(materialized_secret),
        .lease = std::move(lease),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretMaterializationResult failure(contracts::ResultCode result_code,
                                                           std::string message,
                                                           std::string stage,
                                                           std::string source_ref) {
    return SecretMaterializationResult{
        .ok = false,
        .materialized_secret = nullptr,
        .lease = {},
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (ok) {
      return materialized_secret != nullptr && lease.is_valid() && !result_code.has_value() &&
             !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct SecretInspectionResult {
  bool ok = false;
  SecretDescriptor descriptor;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretInspectionResult success(SecretDescriptor descriptor) {
    return SecretInspectionResult{
        .ok = true,
        .descriptor = std::move(descriptor),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretInspectionResult failure(contracts::ResultCode result_code,
                                                      std::string message,
                                                      std::string stage,
                                                      std::string source_ref) {
    return SecretInspectionResult{
        .ok = false,
        .descriptor = {},
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (ok) {
      return descriptor.is_valid() && !result_code.has_value() && !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct SecretLifecycleResult {
  bool ok = false;
  std::string secret_name;
  std::optional<std::string> lease_id;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretLifecycleResult success(std::string secret_name,
                                                     std::optional<std::string> lease_id = std::nullopt) {
    return SecretLifecycleResult{
        .ok = true,
        .secret_name = std::move(secret_name),
        .lease_id = std::move(lease_id),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretLifecycleResult failure(std::string secret_name,
                                                     contracts::ResultCode result_code,
                                                     std::string message,
                                                     std::string stage,
                                                     std::string source_ref,
                                                     std::optional<std::string> lease_id = std::nullopt) {
    return SecretLifecycleResult{
        .ok = false,
        .secret_name = std::move(secret_name),
        .lease_id = std::move(lease_id),
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (secret_name.empty()) {
      return false;
    }

    if (lease_id.has_value() && lease_id->empty()) {
      return false;
    }

    if (ok) {
      return !result_code.has_value() && !error_info.has_value();
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

}  // namespace dasall::infra::secret
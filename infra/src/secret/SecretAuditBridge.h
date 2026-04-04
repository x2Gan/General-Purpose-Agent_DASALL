#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "audit/IAuditLogger.h"
#include "secret/SecretTypes.h"

namespace dasall::infra::secret {

struct SecretAuditBridgeOptions {
  bool audit_required = true;
  std::string detail_ref_prefix = "status://secret/audit/";
  std::string event_id_prefix = "secret-audit-event-";
};

struct SecretAuditEmitResult {
  bool emitted = false;
  AuditEvent audit_event;
  AuditContext audit_context;
  AuditWriteOutcome write_outcome;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static SecretAuditEmitResult success(AuditEvent audit_event,
                                                     AuditContext audit_context,
                                                     AuditWriteOutcome write_outcome) {
    return SecretAuditEmitResult{
        .emitted = true,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static SecretAuditEmitResult failure(AuditEvent audit_event,
                                                     AuditContext audit_context,
                                                     AuditWriteOutcome write_outcome,
                                                     contracts::ResultCode result_code,
                                                     std::string message,
                                                     std::string stage,
                                                     std::string source_ref) {
    return SecretAuditEmitResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = result_code,
        .error_info = detail::make_error_info(result_code,
                                              std::move(message),
                                              std::move(stage),
                                              std::move(source_ref)),
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return emitted && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (emitted) {
      return audit_event.has_required_fields() &&
             audit_event.side_effects_are_serializable() &&
             audit_context.has_non_empty_fields() &&
             !result_code.has_value() && !error_info.has_value() &&
             (write_outcome.is_success() || write_outcome.is_degraded_success());
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct SecretAuditBridgeStatus {
  std::uint64_t emitted_total = 0;
  std::uint64_t emit_failures = 0;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (last_error_code.has_value() &&
        contracts::classify_result_code(*last_error_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    return true;
  }
};

class SecretAuditBridge {
 public:
  explicit SecretAuditBridge(std::shared_ptr<audit::IAuditLogger> audit_logger = nullptr,
                             SecretAuditBridgeOptions options = {});

  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger);

  [[nodiscard]] SecretAuditEmitResult emit_event(SecretAuditEvent event);
  [[nodiscard]] SecretAuditEmitResult emit_access_granted(SecretAuditEvent event);
  [[nodiscard]] SecretAuditEmitResult emit_access_denied(SecretAuditEvent event);
  [[nodiscard]] SecretAuditEmitResult emit_rotate(SecretAuditEvent event);
  [[nodiscard]] SecretAuditEmitResult emit_revoke(SecretAuditEvent event);
  [[nodiscard]] SecretAuditEmitResult emit_fallback(SecretAuditEvent event);

  [[nodiscard]] SecretAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] AuditEvent make_audit_event(const SecretAuditEvent& event);
  [[nodiscard]] AuditContext make_audit_context(const SecretAuditEvent& event) const;
  [[nodiscard]] SecretAuditEmitResult emit_with_action(SecretAuditEvent event,
                                                       SecretAuditAction action);

  void record_success(std::string detail_suffix);
  void record_failure(contracts::ResultCode result_code, std::string detail_suffix);

  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  SecretAuditBridgeOptions options_;
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::secret
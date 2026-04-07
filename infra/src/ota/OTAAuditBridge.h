#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "audit/IAuditLogger.h"
#include "error/ErrorInfo.h"

namespace dasall::infra::ota {

struct OTAAuditRecord {
  std::string actor;
  std::string plan_id;
  std::string package_id;
  std::string target_scope;
  bool succeeded = false;
  std::string evidence_ref;
  std::string rollback_id;
  std::optional<std::string> request_id;
  std::optional<std::string> trace_id;
  std::optional<std::string> task_id;

  [[nodiscard]] bool is_valid() const {
    return !actor.empty() && !plan_id.empty() && !package_id.empty() &&
           !target_scope.empty() && !evidence_ref.empty() && !rollback_id.empty();
  }
};

struct OTAAuditBridgeOptions {
  bool audit_required = true;
  std::string detail_ref_prefix = "status://ota/audit/";
  std::string event_id_prefix = "ota-audit-event-";
};

struct OTAAuditEmitResult {
  bool emitted = false;
  AuditEvent audit_event;
  AuditContext audit_context;
  AuditWriteOutcome write_outcome;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static OTAAuditEmitResult success(AuditEvent audit_event,
                                                  AuditContext audit_context,
                                                  AuditWriteOutcome write_outcome) {
    return OTAAuditEmitResult{
        .emitted = true,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static OTAAuditEmitResult failure(AuditEvent audit_event,
                                                  AuditContext audit_context,
                                                  AuditWriteOutcome write_outcome,
                                                  contracts::ResultCode result_code,
                                                  std::string message,
                                                  std::string stage,
                                                  std::string source_ref) {
    return OTAAuditEmitResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
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
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
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
             audit_context.has_non_empty_fields() && !result_code.has_value() &&
             !error_info.has_value() &&
             (write_outcome.is_success() || write_outcome.is_degraded_success());
    }

    return result_code.has_value() && error_info.has_value() &&
           references_only_contract_error_types();
  }
};

struct OTAAuditBridgeStatus {
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

class OTAAuditBridge {
 public:
  enum class EventKind {
    Precheck = 0,
    Apply,
    Rollback,
  };

  explicit OTAAuditBridge(std::shared_ptr<audit::IAuditLogger> audit_logger = nullptr,
                          OTAAuditBridgeOptions options = {});

  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger);

  [[nodiscard]] OTAAuditEmitResult write_precheck_audit(OTAAuditRecord record);
  [[nodiscard]] OTAAuditEmitResult write_apply_audit(OTAAuditRecord record);
  [[nodiscard]] OTAAuditEmitResult write_rollback_audit(OTAAuditRecord record);

  [[nodiscard]] OTAAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] OTAAuditEmitResult emit_event(OTAAuditRecord record, EventKind kind);
  [[nodiscard]] AuditEvent make_audit_event(const OTAAuditRecord& record,
                                            EventKind kind);
  [[nodiscard]] AuditContext make_audit_context(const OTAAuditRecord& record) const;

  void record_success(std::string detail_suffix);
  void record_failure(contracts::ResultCode result_code, std::string detail_suffix);

  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  OTAAuditBridgeOptions options_;
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::ota
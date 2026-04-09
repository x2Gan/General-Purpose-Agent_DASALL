#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ServiceTypes.h"
#include "audit/IAuditLogger.h"

namespace dasall::services::internal {

enum class ServiceAuditEventKind {
  execution_requested = 0,
  execution_completed,
  compensation_requested,
  compensation_completed,
  fallback_blocked,
};

inline constexpr std::string_view kServiceAuditDefaultWorkerType =
    "services.execution";

[[nodiscard]] inline constexpr std::string_view service_audit_event_name(
    ServiceAuditEventKind kind) {
  switch (kind) {
    case ServiceAuditEventKind::execution_requested:
      return "service.execution.requested";
    case ServiceAuditEventKind::execution_completed:
      return "service.execution.completed";
    case ServiceAuditEventKind::compensation_requested:
      return "service.execution.compensation_requested";
    case ServiceAuditEventKind::compensation_completed:
      return "service.execution.compensation_completed";
    case ServiceAuditEventKind::fallback_blocked:
      return "service.route.fallback_blocked";
  }

  return "service.audit.invalid";
}

struct ServiceAuditBridgeOptions {
  std::string detail_ref_prefix = "status://services/audit/";
  std::string event_id_prefix = "services-audit-event-";
  std::string worker_type = std::string(kServiceAuditDefaultWorkerType);
};

struct ServiceAuditEmitResult {
  bool emitted = false;
  infra::AuditEvent audit_event{};
  infra::AuditContext audit_context{};
  infra::AuditWriteOutcome write_outcome{};
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ServiceAuditEmitResult success(
      infra::AuditEvent audit_event,
      infra::AuditContext audit_context,
      infra::AuditWriteOutcome write_outcome) {
    return ServiceAuditEmitResult{
        .emitted = true,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ServiceAuditEmitResult failure(
      infra::AuditEvent audit_event,
      infra::AuditContext audit_context,
      infra::AuditWriteOutcome write_outcome,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return ServiceAuditEmitResult{
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
                .ref_type = "services.audit",
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

struct ServiceAuditBridgeStatus {
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

class ServiceAuditBridge {
 public:
  explicit ServiceAuditBridge(
      infra::audit::IAuditLogger* audit_logger = nullptr,
      ServiceAuditBridgeOptions options = {});

  void set_audit_logger(infra::audit::IAuditLogger* audit_logger);

  [[nodiscard]] bool has_audit_logger() const {
    return audit_logger_ != nullptr;
  }

  [[nodiscard]] ServiceAuditEmitResult write_execution_requested(
      const ServiceCallContext& context,
      const CapabilityTargetRef& target,
      std::string action,
      std::string execution_id,
      bool require_confirmation);
  [[nodiscard]] ServiceAuditEmitResult write_execution_completed(
      const ServiceCallContext& context,
      const CapabilityTargetRef& target,
      std::string action,
      const ExecutionCommandResult& result);
  [[nodiscard]] ServiceAuditEmitResult write_compensation_requested(
      const ServiceCallContext& context,
      const ExecutionCompensationRequest& request,
      std::string compensation_execution_id);
  [[nodiscard]] ServiceAuditEmitResult write_compensation_completed(
      const ServiceCallContext& context,
      const ExecutionCompensationRequest& request,
      const ExecutionCommandResult& result);
  [[nodiscard]] ServiceAuditEmitResult write_fallback_blocked(
      const ServiceCallContext& context,
      const CapabilityTargetRef& target,
      std::string action,
      std::string execution_id,
      std::string deny_reason,
      std::string action_class);

  [[nodiscard]] ServiceAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] ServiceAuditEmitResult emit_event(
      ServiceAuditEventKind kind,
      const ServiceCallContext& context,
      const CapabilityTargetRef& target,
      std::string action,
      infra::AuditOutcome outcome,
      infra::AuditEvidenceKind evidence_kind,
      std::string evidence_ref,
      std::string detail_ref,
      std::vector<std::string> side_effects);
  [[nodiscard]] infra::AuditEvent make_audit_event(
      ServiceAuditEventKind kind,
      const ServiceCallContext& context,
      const CapabilityTargetRef& target,
      infra::AuditOutcome outcome,
      infra::AuditEvidenceKind evidence_kind,
      std::string evidence_ref,
      std::vector<std::string> side_effects,
      std::int64_t timestamp_ms);
  [[nodiscard]] infra::AuditContext make_audit_context(
      const ServiceCallContext& context) const;

  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  infra::audit::IAuditLogger* audit_logger_ = nullptr;
  ServiceAuditBridgeOptions options_{};
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::services::internal
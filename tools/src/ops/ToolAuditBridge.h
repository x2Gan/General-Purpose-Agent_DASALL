#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ToolManager.h"
#include "audit/IAuditLogger.h"

namespace dasall::tools::ops {

enum class ToolAuditEventKind {
  execution_requested = 0,
  execution_completed,
  execution_failed,
  compensation_executed,
};

inline constexpr std::string_view kToolAuditDefaultWorkerType = "tools.execution";
inline constexpr std::string_view kToolAuditDefaultDetailRef =
    "status://tools/audit/idle";

[[nodiscard]] inline constexpr std::string_view tool_audit_event_name(
    ToolAuditEventKind kind) {
  switch (kind) {
    case ToolAuditEventKind::execution_requested:
      return "tool.execution.requested";
    case ToolAuditEventKind::execution_completed:
      return "tool.execution.completed";
    case ToolAuditEventKind::execution_failed:
      return "tool.execution.failed";
    case ToolAuditEventKind::compensation_executed:
      return "tool.compensation.executed";
  }

  return "tool.audit.invalid";
}

struct ToolAuditBridgeOptions {
  std::string detail_ref_prefix = "status://tools/audit/";
  std::string event_id_prefix = "tools-audit-event-";
  std::string worker_type = std::string(kToolAuditDefaultWorkerType);
};

struct ToolAuditEmitResult {
  bool emitted = false;
  infra::AuditEvent audit_event{};
  infra::AuditContext audit_context{};
  infra::AuditWriteOutcome write_outcome{};
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static ToolAuditEmitResult success(
      infra::AuditEvent audit_event,
      infra::AuditContext audit_context,
      infra::AuditWriteOutcome write_outcome) {
    return ToolAuditEmitResult{
        .emitted = true,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = std::move(write_outcome),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static ToolAuditEmitResult failure(
      infra::AuditEvent audit_event,
      infra::AuditContext audit_context,
      infra::AuditWriteOutcome write_outcome,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return ToolAuditEmitResult{
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
                .ref_type = "tools.audit",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] static ToolAuditEmitResult skipped() {
    return ToolAuditEmitResult{
        .emitted = false,
        .audit_event = {},
        .audit_context = {},
        .write_outcome = infra::AuditWriteOutcome{
            .accepted = false,
            .persisted = false,
            .fallback_used = false,
            .error_code = std::nullopt,
        },
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return emitted && !result_code.has_value();
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_skipped() const {
    return !emitted && !result_code.has_value() && !error_info.has_value() &&
           !audit_event.has_required_fields() && !audit_context.has_non_empty_fields();
  }

  [[nodiscard]] bool is_valid() const {
    if (is_skipped()) {
      return true;
    }

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

struct ToolAuditBridgeStatus {
  std::uint64_t emitted_total = 0;
  std::uint64_t emit_failures = 0;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref = std::string(kToolAuditDefaultDetailRef);

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

class ToolAuditBridge {
 public:
  explicit ToolAuditBridge(
      infra::audit::IAuditLogger* audit_logger = nullptr,
      ToolAuditBridgeOptions options = {});

  void set_audit_logger(infra::audit::IAuditLogger* audit_logger);

  [[nodiscard]] bool has_audit_logger() const {
    return audit_logger_ != nullptr;
  }

  [[nodiscard]] ToolAuditEmitResult emit_requested(
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context);
  [[nodiscard]] ToolAuditEmitResult emit_completed(
      const ToolInvocationEnvelope& envelope);
  [[nodiscard]] ToolAuditEmitResult emit_failed(
      const ToolInvocationEnvelope& envelope);
  [[nodiscard]] ToolAuditEmitResult emit_compensation(
      const CompensationRequest& request,
      const ToolInvocationEnvelope& envelope);
  [[nodiscard]] ToolAuditBridgeStatus get_status() const;

  [[nodiscard]] static manager::ToolAuditHooks bind_hooks(
      const std::shared_ptr<ToolAuditBridge>& bridge);

 private:
  struct PendingInvocationFacts {
    std::string request_id;
    std::string session_id;
    std::string trace_id;
    std::string goal_id;
    std::string worker_task_id;
    std::string caller_domain;
    std::string tool_name;
    bool confirmation_present = false;
  };

  [[nodiscard]] ToolAuditEmitResult emit_event(
      ToolAuditEventKind kind,
      const std::string& tool_call_id,
      std::string tool_name,
      infra::AuditOutcome outcome,
      infra::AuditEvidenceKind evidence_kind,
      std::string evidence_ref,
      std::string detail_ref,
      std::vector<std::string> side_effects,
      std::optional<PendingInvocationFacts> pending_facts,
      std::optional<std::string> request_id,
      std::optional<std::string> goal_id,
      std::optional<std::string> worker_task_id,
      std::int64_t timestamp_ms);
  [[nodiscard]] infra::AuditEvent make_audit_event(
      ToolAuditEventKind kind,
      const std::string& tool_call_id,
      std::string tool_name,
      infra::AuditOutcome outcome,
      infra::AuditEvidenceKind evidence_kind,
      std::string evidence_ref,
      std::vector<std::string> side_effects,
      std::int64_t timestamp_ms);
  [[nodiscard]] infra::AuditContext make_audit_context(
      const std::optional<PendingInvocationFacts>& pending_facts,
      std::optional<std::string> request_id,
      std::optional<std::string> goal_id,
      std::optional<std::string> worker_task_id) const;

  void remember_request(
      const contracts::ToolRequest& request,
      const ToolInvocationContext& context);
  [[nodiscard]] std::optional<PendingInvocationFacts> find_pending_facts(
      const std::optional<std::string>& tool_call_id) const;
  void record_success(const std::string& detail_ref);
  void record_failure(
      std::optional<contracts::ResultCode> result_code,
      const std::string& detail_ref);

  infra::audit::IAuditLogger* audit_logger_ = nullptr;
  ToolAuditBridgeOptions options_{};
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_ = std::string(kToolAuditDefaultDetailRef);
  std::unordered_map<std::string, PendingInvocationFacts> pending_invocations_;
};

}  // namespace dasall::tools::ops
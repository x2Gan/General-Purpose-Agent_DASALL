#include "PolicyAuditBridge.h"

#include <chrono>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::infra::policy {
namespace {

constexpr std::string_view kPolicyAuditBridgeWorkerType = "infra.policy";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value, std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] std::string make_side_effect(std::string_view key,
                                           std::string_view value) {
  return std::string(key) + ":" + std::string(value);
}

void append_side_effect(std::vector<std::string>* side_effects,
                        std::string_view key,
                        std::string_view value) {
  if (!value.empty()) {
    side_effects->push_back(make_side_effect(key, value));
  }
}

void append_number_side_effect(std::vector<std::string>* side_effects,
                               std::string_view key,
                               std::uint64_t value) {
  if (value > 0U) {
    side_effects->push_back(make_side_effect(key, std::to_string(value)));
  }
}

[[nodiscard]] std::string reason_code_from_result(const PolicyOpResult& result,
                                                  std::string_view success_fallback) {
  if (result.error_info.has_value() && !result.error_info->details.message.empty()) {
    const std::size_t separator = result.error_info->details.message.find(':');
    if (separator != std::string::npos && separator > 0U) {
      return result.error_info->details.message.substr(0, separator);
    }

    return result.error_info->details.message;
  }

  return std::string(success_fallback);
}

[[nodiscard]] AuditOutcome outcome_from_result(const PolicyOpResult& result) {
  if (result.applied) {
    return AuditOutcome::Succeeded;
  }

  return result.result_code == contracts::ResultCode::PolicyDenied ? AuditOutcome::Rejected
                                                                   : AuditOutcome::Failed;
}

[[nodiscard]] AuditWriteOutcome make_missing_logger_outcome() {
  return AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = contracts::ResultCode::RuntimeRetryExhausted,
  };
}

[[nodiscard]] std::string make_result_evidence_ref(std::string_view category,
                                                   std::string_view ref_id,
                                                   const PolicyOpResult& result) {
  if (!result.snapshot_id.empty() && result.generation > 0U) {
    return std::string("policy:snapshot/") + result.snapshot_id + "/generation/" +
           std::to_string(result.generation);
  }

  return std::string("policy:") + std::string(category) + "/" + std::string(ref_id);
}

}  // namespace

PolicyAuditBridge::PolicyAuditBridge(std::shared_ptr<audit::IAuditLogger> audit_logger,
                                     PolicyAuditBridgeOptions options)
    : audit_logger_(std::move(audit_logger)),
      options_(std::move(options)),
      last_detail_ref_(normalized_or(options_.detail_ref_prefix,
                                     "status://policy/audit/") + "idle") {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://policy/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "policy-audit-event-");
}

void PolicyAuditBridge::set_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

PolicyAuditEmitResult PolicyAuditBridge::emit_load_result(const PolicyBundle& bundle,
                                                          const PolicyOpResult& result) {
  return emit_event(make_load_event(bundle, result),
                    make_default_context(),
                    result.applied ? "load/success" : "load/failure");
}

PolicyAuditEmitResult PolicyAuditBridge::emit_patch_result(const PolicyPatch& patch,
                                                           const PolicyOpResult& result) {
  return emit_event(make_patch_event(patch, result),
                    make_default_context(),
                    result.applied ? "apply_patch/success" : "apply_patch/failure");
}

PolicyAuditEmitResult PolicyAuditBridge::emit_rollback_result(
    const std::string& rollback_target_snapshot_id,
    const PolicyOpResult& result) {
  return emit_event(make_rollback_event(rollback_target_snapshot_id, result),
                    make_default_context(),
                    result.applied ? "rollback/success" : "rollback/failure");
}

PolicyAuditEmitResult PolicyAuditBridge::emit_high_risk_deny(
    const PolicyQueryContext& query,
    const PolicyDecisionRef& decision) {
  return emit_event(make_deny_event(query, decision),
                    make_query_context(query),
                    "query_deny/success");
}

PolicyAuditBridgeStatus PolicyAuditBridge::get_status() const {
  return PolicyAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? options_.detail_ref_prefix + "idle"
                                             : last_detail_ref_,
  };
}

PolicyAuditEmitResult PolicyAuditBridge::emit_event(AuditEvent audit_event,
                                                    AuditContext audit_context,
                                                    const std::string& detail_suffix) {
  if (!audit_logger_) {
    const AuditWriteOutcome write_outcome = make_missing_logger_outcome();
    record_failure(write_outcome.error_code, detail_suffix + "/missing_logger");
    return PolicyAuditEmitResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = write_outcome,
    };
  }

  const AuditWriteOutcome write_outcome =
      audit_logger_->write_audit(audit_event, audit_context);
  const bool emitted = write_outcome.is_success() || write_outcome.is_degraded_success();
  if (!emitted) {
    record_failure(write_outcome.error_code, detail_suffix + "/write_failed");
    return PolicyAuditEmitResult{
        .emitted = false,
        .audit_event = std::move(audit_event),
        .audit_context = std::move(audit_context),
        .write_outcome = write_outcome,
    };
  }

  ++emitted_total_;
  record_success(detail_suffix);
  return PolicyAuditEmitResult{
      .emitted = true,
      .audit_event = std::move(audit_event),
      .audit_context = std::move(audit_context),
      .write_outcome = write_outcome,
  };
}

AuditEvent PolicyAuditBridge::make_load_event(const PolicyBundle& bundle,
                                              const PolicyOpResult& result) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "bundle_id", bundle.bundle_id);
  append_side_effect(&side_effects, "reason_code",
                     reason_code_from_result(result, "policy_load_applied"));
  append_side_effect(&side_effects, "source", bundle.source);
  append_side_effect(&side_effects, "snapshot_id", result.snapshot_id);
  append_number_side_effect(&side_effects, "generation", result.generation);

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("policy.load"),
      .actor = std::string(kPolicyAuditBridgeWorkerType),
      .target = std::string("policy_bundle:") + bundle.bundle_id,
      .outcome = outcome_from_result(result),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = make_result_evidence_ref("bundle", bundle.bundle_id, result),
      },
      .side_effects = std::move(side_effects),
      .timestamp = current_time_unix_ms(),
  };
}

AuditEvent PolicyAuditBridge::make_patch_event(const PolicyPatch& patch,
                                               const PolicyOpResult& result) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "patch_id", patch.patch_id);
  append_number_side_effect(&side_effects, "base_generation", patch.base_generation);
  append_side_effect(&side_effects, "reason_code",
                     reason_code_from_result(result, "policy_patch_applied"));
  append_side_effect(&side_effects, "snapshot_id", result.snapshot_id);
  append_number_side_effect(&side_effects, "generation", result.generation);

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("policy.apply_patch"),
      .actor = patch.actor.empty() ? std::string(kPolicyAuditBridgeWorkerType)
                                   : patch.actor,
      .target = std::string("policy_patch:") + patch.patch_id,
      .outcome = outcome_from_result(result),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = make_result_evidence_ref("patch", patch.patch_id, result),
      },
      .side_effects = std::move(side_effects),
      .timestamp = current_time_unix_ms(),
  };
}

AuditEvent PolicyAuditBridge::make_rollback_event(
    const std::string& rollback_target_snapshot_id,
    const PolicyOpResult& result) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "rollback_target", rollback_target_snapshot_id);
  append_side_effect(&side_effects, "reason_code",
                     reason_code_from_result(result, "policy_rollback_applied"));
  append_side_effect(&side_effects, "snapshot_id", result.snapshot_id);
  append_number_side_effect(&side_effects, "generation", result.generation);

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("policy.rollback"),
      .actor = std::string(kPolicyAuditBridgeWorkerType),
      .target = std::string("policy_snapshot:") + rollback_target_snapshot_id,
      .outcome = outcome_from_result(result),
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = make_result_evidence_ref("rollback", rollback_target_snapshot_id, result),
      },
      .side_effects = std::move(side_effects),
      .timestamp = current_time_unix_ms(),
  };
}

AuditEvent PolicyAuditBridge::make_deny_event(const PolicyQueryContext& query,
                                              const PolicyDecisionRef& decision) {
  std::vector<std::string> side_effects;
  append_side_effect(&side_effects, "reason_code", decision.reason_code);
  append_side_effect(&side_effects, "snapshot_id", decision.snapshot_id);
  append_number_side_effect(&side_effects, "generation", decision.generation);
  append_side_effect(&side_effects, "detail_ref", decision.evidence_ref);

  return AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string("policy.query_deny"),
      .actor = query.actor_ref,
      .target = std::string("policy_target:") + query.module + ":" + query.target_type + ":" +
                query.target_ref,
      .outcome = AuditOutcome::Rejected,
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = std::string("policy:decision/") + decision.snapshot_id + "/" + decision.reason_code,
      },
      .side_effects = std::move(side_effects),
      .timestamp = current_time_unix_ms(),
  };
}

AuditContext PolicyAuditBridge::make_default_context() const {
  return AuditContext{
      .request_id = std::string(kAuditContextUnknown),
      .session_id = std::string(kAuditContextUnknown),
      .trace_id = std::string(kAuditContextUnknown),
      .task_id = std::string(kAuditContextUnknown),
      .parent_task_id = std::string(kAuditContextUnknown),
      .lease_id = std::string(kAuditContextUnknown),
      .worker_type = std::string(kPolicyAuditBridgeWorkerType),
  };
}

AuditContext PolicyAuditBridge::make_query_context(
    const PolicyQueryContext& query) const {
  return AuditContext{
      .request_id = query.request_id,
      .session_id = query.session_id,
      .trace_id = query.trace_id,
      .task_id = query.task_id,
      .parent_task_id = std::string(kAuditContextUnknown),
      .lease_id = std::string(kAuditContextUnknown),
      .worker_type = query.module.empty() ? std::string(kPolicyAuditBridgeWorkerType)
                                          : query.module,
  };
}

void PolicyAuditBridge::record_success(const std::string& detail_suffix) {
  last_error_code_.reset();
  last_detail_ref_ = options_.detail_ref_prefix + detail_suffix;
}

void PolicyAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_suffix) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = options_.detail_ref_prefix + detail_suffix;
}

}  // namespace dasall::infra::policy
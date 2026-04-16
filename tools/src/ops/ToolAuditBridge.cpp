#include "ops/ToolAuditBridge.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace dasall::tools::ops {

namespace {

constexpr std::string_view kToolAuditBridgeSourceRef = "ToolAuditBridge";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string normalized_or(std::string value,
                                        std::string_view fallback) {
  if (value.empty()) {
    return std::string(fallback);
  }

  return value;
}

[[nodiscard]] bool has_confirmation_fact(const ToolInvocationContext& context) {
  if (!context.confirmation_facts.has_value()) {
    return false;
  }

  return std::any_of(
      context.confirmation_facts->begin(),
      context.confirmation_facts->end(),
      [](const ToolConfirmationFact& fact) { return fact.has_consistent_values(); });
}

[[nodiscard]] std::string result_code_name(contracts::ResultCode result_code) {
  switch (result_code) {
    case contracts::ResultCode::ValidationFieldMissing:
      return "ValidationFieldMissing";
    case contracts::ResultCode::PolicyDenied:
      return "PolicyDenied";
    case contracts::ResultCode::ToolExecutionFailed:
      return "ToolExecutionFailed";
    case contracts::ResultCode::ProviderTimeout:
      return "ProviderTimeout";
    case contracts::ResultCode::RuntimeRetryExhausted:
      return "RuntimeRetryExhausted";
  }

  return "Unknown";
}

[[nodiscard]] std::string invocation_kind_name(
    contracts::ToolInvocationKind invocation_kind) {
  switch (invocation_kind) {
    case contracts::ToolInvocationKind::InformationQuery:
      return "information_query";
    case contracts::ToolInvocationKind::Action:
      return "action";
    case contracts::ToolInvocationKind::Workflow:
      return "workflow";
    case contracts::ToolInvocationKind::AgentDelegation:
      return "agent_delegation";
    case contracts::ToolInvocationKind::Diagnostic:
      return "diagnostic";
    case contracts::ToolInvocationKind::Unspecified:
      break;
  }

  return "unspecified";
}

[[nodiscard]] std::string describe_write_failure(
    const infra::AuditWriteOutcome& write_outcome) {
  if (!write_outcome.has_consistent_state()) {
    return "tool audit logger returned an inconsistent write outcome";
  }

  if (write_outcome.error_code.has_value()) {
    return "tool audit logger returned a failing write outcome";
  }

  return "tool audit logger did not report success or degraded success";
}

[[nodiscard]] infra::AuditWriteOutcome make_write_failure_outcome(
    contracts::ResultCode result_code) {
  return infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = result_code,
  };
}

void append_unique_side_effect(std::vector<std::string>* side_effects,
                               std::string value) {
  if (value.empty()) {
    return;
  }

  if (std::find(side_effects->begin(), side_effects->end(), value) ==
      side_effects->end()) {
    side_effects->push_back(std::move(value));
  }
}

void append_key_value_side_effect(std::vector<std::string>* side_effects,
                                  std::string_view key,
                                  const std::string& value) {
  if (!value.empty()) {
    append_unique_side_effect(side_effects, std::string(key) + ":" + value);
  }
}

void append_optional_value(std::vector<std::string>* side_effects,
                           std::string_view key,
                           const std::optional<std::string>& value) {
  if (value.has_value() && !value->empty()) {
    append_key_value_side_effect(side_effects, key, *value);
  }
}

void append_optional_values(std::vector<std::string>* side_effects,
                            std::string_view key,
                            const std::optional<std::vector<std::string>>& values) {
  if (!values.has_value()) {
    return;
  }

  for (const auto& value : *values) {
    append_key_value_side_effect(side_effects, key, value);
  }
}

void append_bool_side_effect(std::vector<std::string>* side_effects,
                             std::string_view key,
                             bool value) {
  append_unique_side_effect(side_effects,
                            std::string(key) + ":" + (value ? "true" : "false"));
}

[[nodiscard]] std::string make_tool_call_ref(const std::string& tool_call_id) {
  return tool_call_id.empty() ? std::string("tool_call://unknown")
                              : std::string("tool_call://") + tool_call_id;
}

[[nodiscard]] std::string make_tool_target(const std::string& tool_name) {
  return tool_name.empty() ? std::string("tool:unknown")
                           : std::string("tool:") + tool_name;
}

[[nodiscard]] std::string make_compensation_ref(
    const CompensationRequest& request,
    const contracts::ToolResult* result) {
  if (request.tool_call_id.has_value() && !request.tool_call_id->empty()) {
    return std::string("compensation://") + *request.tool_call_id;
  }

  if (result != nullptr && result->tool_call_id.has_value() &&
      !result->tool_call_id->empty()) {
    return std::string("compensation://") + *result->tool_call_id;
  }

  return std::string("compensation://unknown");
}

[[nodiscard]] std::optional<std::string> first_non_empty_ref(
    const std::optional<std::vector<std::string>>& refs) {
  if (!refs.has_value()) {
    return std::nullopt;
  }

  for (const auto& ref : *refs) {
    if (!ref.empty()) {
      return ref;
    }
  }

  return std::nullopt;
}

[[nodiscard]] infra::AuditOutcome failed_outcome(
    const ToolInvocationEnvelope& envelope) {
  if (!envelope.tool_result.has_value() || !envelope.tool_result->error.has_value()) {
    return infra::AuditOutcome::Failed;
  }

  if (envelope.tool_result->error->failure_type.has_value() &&
      *envelope.tool_result->error->failure_type ==
          contracts::ResultCodeCategory::Policy) {
    return infra::AuditOutcome::Rejected;
  }

  return infra::AuditOutcome::Failed;
}

[[nodiscard]] infra::AuditOutcome compensation_outcome(
    const ToolInvocationEnvelope& envelope) {
  if (envelope.tool_result.has_value() &&
      envelope.tool_result->success.value_or(false)) {
    return infra::AuditOutcome::Succeeded;
  }

  return failed_outcome(envelope);
}

[[nodiscard]] bool looks_like_compensation_terminal(
    const ToolInvocationEnvelope& envelope) {
  if (!envelope.tool_result.has_value()) {
    return false;
  }

  return !envelope.tool_result->request_id.has_value() &&
         !envelope.tool_result->tool_name.has_value();
}

}  // namespace

ToolAuditBridge::ToolAuditBridge(infra::audit::IAuditLogger* audit_logger,
                                 ToolAuditBridgeOptions options)
    : audit_logger_(audit_logger),
      options_(std::move(options)),
      last_detail_ref_(std::string(kToolAuditDefaultDetailRef)) {
  options_.detail_ref_prefix = normalized_or(options_.detail_ref_prefix,
                                             "status://tools/audit/");
  options_.event_id_prefix = normalized_or(options_.event_id_prefix,
                                           "tools-audit-event-");
  options_.worker_type = normalized_or(options_.worker_type,
                                       kToolAuditDefaultWorkerType);
}

void ToolAuditBridge::set_audit_logger(infra::audit::IAuditLogger* audit_logger) {
  audit_logger_ = audit_logger;
}

ToolAuditEmitResult ToolAuditBridge::emit_requested(
    const contracts::ToolRequest& request,
    const ToolInvocationContext& context) {
  remember_request(request, context);

  std::vector<std::string> side_effects;
  append_optional_value(&side_effects, "request_id", request.request_id);
  append_optional_value(&side_effects, "tool_name", request.tool_name);
  if (request.invocation_kind.has_value()) {
    append_key_value_side_effect(
        &side_effects,
        "invocation_kind",
        invocation_kind_name(*request.invocation_kind));
  }
  append_optional_value(&side_effects, "caller_domain", context.caller_domain);
  append_bool_side_effect(&side_effects,
                          "confirmation_present",
                          has_confirmation_fact(context));
  append_optional_values(&side_effects, "tag", request.tags);

  const auto tool_call_id = request.tool_call_id.value_or(std::string{});
  const auto detail_ref = make_tool_call_ref(tool_call_id);
  return emit_event(
      ToolAuditEventKind::execution_requested,
      tool_call_id,
      request.tool_name.value_or(std::string{}),
      infra::AuditOutcome::Escalated,
      infra::AuditEvidenceKind::ToolResult,
      detail_ref,
      detail_ref,
      std::move(side_effects),
      find_pending_facts(request.tool_call_id),
      request.request_id,
      request.goal_id,
      request.worker_task_id,
      request.created_at.value_or(current_time_unix_ms()));
}

ToolAuditEmitResult ToolAuditBridge::emit_completed(
    const ToolInvocationEnvelope& envelope) {
  const auto tool_call_id = envelope.tool_result.has_value() &&
                                    envelope.tool_result->tool_call_id.has_value()
                                ? *envelope.tool_result->tool_call_id
                                : std::string{};
  std::vector<std::string> side_effects;
  append_optional_value(&side_effects, "route_kind",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->route_kind
                            : std::optional<std::string>{});
  append_optional_value(&side_effects, "decision_reason",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->decision_reason
                            : std::optional<std::string>{});
  append_optional_value(&side_effects, "plugin_id",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->plugin_id
                            : std::optional<std::string>{});
  if (envelope.tool_result.has_value()) {
    append_optional_value(&side_effects, "request_id", envelope.tool_result->request_id);
    append_optional_value(&side_effects, "tool_name", envelope.tool_result->tool_name);
    if (envelope.tool_result->duration_ms.has_value()) {
      append_key_value_side_effect(&side_effects,
                                   "duration_ms",
                                   std::to_string(*envelope.tool_result->duration_ms));
    }
    append_optional_values(&side_effects, "side_effect", envelope.tool_result->side_effects);
  }
  append_optional_values(&side_effects, "evidence_ref", envelope.evidence_refs);
  if (envelope.observation_digest.has_value() &&
      envelope.observation_digest->confidence.has_value()) {
    append_key_value_side_effect(
        &side_effects,
        "digest_confidence",
        std::to_string(*envelope.observation_digest->confidence));
  }

  const auto detail_ref = make_tool_call_ref(tool_call_id);
  return emit_event(
      ToolAuditEventKind::execution_completed,
      tool_call_id,
      envelope.tool_result.has_value() && envelope.tool_result->tool_name.has_value()
          ? *envelope.tool_result->tool_name
          : std::string{},
      infra::AuditOutcome::Succeeded,
      infra::AuditEvidenceKind::ToolResult,
      first_non_empty_ref(envelope.evidence_refs).value_or(detail_ref),
      detail_ref,
      std::move(side_effects),
      find_pending_facts(envelope.tool_result.has_value()
                             ? envelope.tool_result->tool_call_id
                             : std::optional<std::string>{}),
      envelope.tool_result.has_value() ? envelope.tool_result->request_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() ? envelope.tool_result->goal_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() ? envelope.tool_result->worker_task_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() && envelope.tool_result->completed_at.has_value()
          ? *envelope.tool_result->completed_at
          : current_time_unix_ms());
}

ToolAuditEmitResult ToolAuditBridge::emit_failed(
    const ToolInvocationEnvelope& envelope) {
  if (looks_like_compensation_terminal(envelope)) {
    return ToolAuditEmitResult::skipped();
  }

  const auto tool_call_id = envelope.tool_result.has_value() &&
                                    envelope.tool_result->tool_call_id.has_value()
                                ? *envelope.tool_result->tool_call_id
                                : std::string{};
  std::vector<std::string> side_effects;
  append_optional_value(&side_effects, "route_kind",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->route_kind
                            : std::optional<std::string>{});
  append_optional_value(&side_effects, "decision_reason",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->decision_reason
                            : std::optional<std::string>{});
  append_optional_value(&side_effects, "plugin_id",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->plugin_id
                            : std::optional<std::string>{});
  append_optional_value(&side_effects, "failure_reason", envelope.failure_reason_code);
  append_optional_values(&side_effects, "evidence_ref", envelope.evidence_refs);
  if (envelope.tool_result.has_value()) {
    append_optional_value(&side_effects, "request_id", envelope.tool_result->request_id);
    append_optional_value(&side_effects, "tool_name", envelope.tool_result->tool_name);
    append_optional_values(&side_effects, "side_effect", envelope.tool_result->side_effects);
    if (envelope.tool_result->error.has_value()) {
      if (envelope.tool_result->error->details.code.has_value()) {
        append_key_value_side_effect(
            &side_effects,
            "result_code",
            result_code_name(static_cast<contracts::ResultCode>(
                *envelope.tool_result->error->details.code)));
      }
      append_key_value_side_effect(
          &side_effects,
          "error_stage",
          envelope.tool_result->error->details.stage);
      append_key_value_side_effect(
          &side_effects,
          "error_source",
          envelope.tool_result->error->source_ref.ref_id);
    }
  }

  const auto detail_ref = make_tool_call_ref(tool_call_id);
  return emit_event(
      ToolAuditEventKind::execution_failed,
      tool_call_id,
      envelope.tool_result.has_value() && envelope.tool_result->tool_name.has_value()
          ? *envelope.tool_result->tool_name
          : std::string{},
      failed_outcome(envelope),
      infra::AuditEvidenceKind::ToolResult,
      first_non_empty_ref(envelope.evidence_refs).value_or(detail_ref),
      detail_ref,
      std::move(side_effects),
      find_pending_facts(envelope.tool_result.has_value()
                             ? envelope.tool_result->tool_call_id
                             : std::optional<std::string>{}),
      envelope.tool_result.has_value() ? envelope.tool_result->request_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() ? envelope.tool_result->goal_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() ? envelope.tool_result->worker_task_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() && envelope.tool_result->completed_at.has_value()
          ? *envelope.tool_result->completed_at
          : current_time_unix_ms());
}

ToolAuditEmitResult ToolAuditBridge::emit_compensation(
    const CompensationRequest& request,
    const ToolInvocationEnvelope& envelope) {
  std::vector<std::string> side_effects;
  append_optional_value(&side_effects, "compensation_action", request.compensation_action);
  append_optional_value(&side_effects, "target_ref", request.target_ref);
  append_optional_value(&side_effects, "reason_code", request.reason_code);
  append_optional_values(&side_effects, "evidence_ref", request.evidence_refs);
  append_optional_values(&side_effects, "evidence_ref", envelope.evidence_refs);
  append_optional_value(&side_effects, "failure_reason", envelope.failure_reason_code);
  append_optional_value(&side_effects, "route_kind",
                        envelope.route_facts.has_value()
                            ? envelope.route_facts->route_kind
                            : std::optional<std::string>{});
  if (envelope.tool_result.has_value()) {
    append_optional_values(&side_effects, "side_effect", envelope.tool_result->side_effects);
    append_bool_side_effect(&side_effects,
                            "success",
                            envelope.tool_result->success.value_or(false));
  }

  const auto detail_ref = make_compensation_ref(
      request,
      envelope.tool_result.has_value() ? &*envelope.tool_result : nullptr);
  const auto tool_call_id = request.tool_call_id.value_or(
      envelope.tool_result.has_value() && envelope.tool_result->tool_call_id.has_value()
          ? *envelope.tool_result->tool_call_id
          : std::string{});
  const auto pending_facts = find_pending_facts(
      request.tool_call_id.has_value() ? request.tool_call_id
                                       : envelope.tool_result.has_value()
                                             ? envelope.tool_result->tool_call_id
                                             : std::optional<std::string>{});
  return emit_event(
      ToolAuditEventKind::compensation_executed,
      tool_call_id,
      pending_facts.has_value() ? pending_facts->tool_name : std::string{},
      compensation_outcome(envelope),
      infra::AuditEvidenceKind::RecoveryOutcome,
      first_non_empty_ref(request.evidence_refs).value_or(detail_ref),
      detail_ref,
      std::move(side_effects),
      std::move(pending_facts),
      envelope.tool_result.has_value() ? envelope.tool_result->request_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() ? envelope.tool_result->goal_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() ? envelope.tool_result->worker_task_id
                                       : std::optional<std::string>{},
      envelope.tool_result.has_value() && envelope.tool_result->completed_at.has_value()
          ? *envelope.tool_result->completed_at
          : current_time_unix_ms());
}

ToolAuditBridgeStatus ToolAuditBridge::get_status() const {
  return ToolAuditBridgeStatus{
      .emitted_total = emitted_total_,
      .emit_failures = emit_failures_,
      .degraded = emit_failures_ > 0 || last_error_code_.has_value(),
      .last_error_code = last_error_code_,
      .detail_ref = last_detail_ref_.empty() ? std::string(kToolAuditDefaultDetailRef)
                                             : last_detail_ref_,
  };
}

manager::ToolAuditHooks ToolAuditBridge::bind_hooks(
    const std::shared_ptr<ToolAuditBridge>& bridge) {
  if (!bridge) {
    return manager::ToolAuditHooks{};
  }

  return manager::ToolAuditHooks{
      .on_requested = [bridge](const contracts::ToolRequest& request,
                               const ToolInvocationContext& context) {
        static_cast<void>(bridge->emit_requested(request, context));
      },
      .on_completed = [bridge](const ToolInvocationEnvelope& envelope) {
        static_cast<void>(bridge->emit_completed(envelope));
      },
      .on_failed = [bridge](const ToolInvocationEnvelope& envelope) {
        static_cast<void>(bridge->emit_failed(envelope));
      },
      .on_compensation = [bridge](const CompensationRequest& request,
                                  const ToolInvocationEnvelope& envelope) {
        static_cast<void>(bridge->emit_compensation(request, envelope));
      },
  };
}

ToolAuditEmitResult ToolAuditBridge::emit_event(
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
    std::int64_t timestamp_ms) {
  if (detail_ref.empty()) {
    detail_ref = options_.detail_ref_prefix + "invalid_event";
  }

  if (!audit_logger_) {
    record_failure(contracts::ResultCode::RuntimeRetryExhausted, detail_ref);
    return ToolAuditEmitResult::failure(
        {},
        {},
        make_write_failure_outcome(contracts::ResultCode::RuntimeRetryExhausted),
        contracts::ResultCode::RuntimeRetryExhausted,
        "tool audit bridge requires an infra::audit::IAuditLogger sink before audit events can be persisted",
        "tools.audit",
        std::string(kToolAuditBridgeSourceRef));
  }

  auto audit_event = make_audit_event(kind,
                                      tool_call_id,
                                      std::move(tool_name),
                                      outcome,
                                      evidence_kind,
                                      std::move(evidence_ref),
                                      std::move(side_effects),
                                      timestamp_ms);
  auto audit_context = make_audit_context(pending_facts,
                                          std::move(request_id),
                                          std::move(goal_id),
                                          std::move(worker_task_id));
  if (!audit_event.has_required_fields() ||
      !audit_event.side_effects_are_serializable() ||
      !audit_context.has_non_empty_fields()) {
    record_failure(contracts::ResultCode::ValidationFieldMissing, detail_ref);
    return ToolAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        make_write_failure_outcome(contracts::ResultCode::ValidationFieldMissing),
        contracts::ResultCode::ValidationFieldMissing,
        "tool audit bridge produced an invalid audit payload",
        "tools.audit",
        std::string(kToolAuditBridgeSourceRef));
  }

  const auto write_outcome = audit_logger_->write_audit(audit_event, audit_context);
  if (!write_outcome.is_success() && !write_outcome.is_degraded_success()) {
    const auto result_code =
        write_outcome.error_code.value_or(contracts::ResultCode::RuntimeRetryExhausted);
    record_failure(result_code, detail_ref);
    return ToolAuditEmitResult::failure(
        std::move(audit_event),
        std::move(audit_context),
        write_outcome,
        result_code,
        describe_write_failure(write_outcome),
        "tools.audit",
        std::string(kToolAuditBridgeSourceRef));
  }

  ++emitted_total_;
  record_success(detail_ref);
  return ToolAuditEmitResult::success(
      std::move(audit_event),
      std::move(audit_context),
      write_outcome);
}

infra::AuditEvent ToolAuditBridge::make_audit_event(
    ToolAuditEventKind kind,
    const std::string& tool_call_id,
    std::string tool_name,
    infra::AuditOutcome outcome,
    infra::AuditEvidenceKind evidence_kind,
    std::string evidence_ref,
    std::vector<std::string> side_effects,
    std::int64_t timestamp_ms) {
  return infra::AuditEvent{
      .event_id = options_.event_id_prefix + std::to_string(next_event_sequence_++),
      .action = std::string(tool_audit_event_name(kind)),
      .actor = make_tool_call_ref(tool_call_id),
      .target = make_tool_target(std::move(tool_name)),
      .outcome = outcome,
      .evidence_ref = {
          .kind = evidence_kind,
          .ref = std::move(evidence_ref),
      },
      .side_effects = std::move(side_effects),
      .timestamp = timestamp_ms > 0 ? timestamp_ms : current_time_unix_ms(),
  };
}

infra::AuditContext ToolAuditBridge::make_audit_context(
    const std::optional<PendingInvocationFacts>& pending_facts,
    std::optional<std::string> request_id,
    std::optional<std::string> goal_id,
    std::optional<std::string> worker_task_id) const {
  return infra::AuditContext{
      .request_id = normalized_or(
          request_id.value_or(pending_facts.has_value() ? pending_facts->request_id
                                                        : std::string{}),
          infra::kAuditContextUnknown),
      .session_id = normalized_or(
          pending_facts.has_value() ? pending_facts->session_id : std::string{},
          infra::kAuditContextUnknown),
      .trace_id = normalized_or(
          pending_facts.has_value() ? pending_facts->trace_id : std::string{},
          infra::kAuditContextUnknown),
      .task_id = normalized_or(
          worker_task_id.value_or(pending_facts.has_value() ? pending_facts->worker_task_id
                                                            : std::string{}),
          infra::kAuditContextUnknown),
      .parent_task_id = normalized_or(
          goal_id.value_or(pending_facts.has_value() ? pending_facts->goal_id
                                                     : std::string{}),
          infra::kAuditContextUnknown),
      .lease_id = std::string(infra::kAuditContextUnknown),
      .worker_type = options_.worker_type,
  };
}

void ToolAuditBridge::remember_request(const contracts::ToolRequest& request,
                                       const ToolInvocationContext& context) {
  if (!request.tool_call_id.has_value() || request.tool_call_id->empty()) {
    return;
  }

  pending_invocations_[*request.tool_call_id] = PendingInvocationFacts{
      .request_id = request.request_id.value_or(std::string{}),
      .session_id = context.session_id.value_or(std::string{}),
      .trace_id = context.trace.trace_id.value_or(std::string{}),
      .goal_id = request.goal_id.value_or(std::string{}),
      .worker_task_id = request.worker_task_id.value_or(std::string{}),
      .caller_domain = context.caller_domain.value_or(std::string{}),
      .tool_name = request.tool_name.value_or(std::string{}),
      .confirmation_present = has_confirmation_fact(context),
  };
}

std::optional<ToolAuditBridge::PendingInvocationFacts>
ToolAuditBridge::find_pending_facts(
    const std::optional<std::string>& tool_call_id) const {
  if (!tool_call_id.has_value() || tool_call_id->empty()) {
    return std::nullopt;
  }

  const auto found = pending_invocations_.find(*tool_call_id);
  if (found == pending_invocations_.end()) {
    return std::nullopt;
  }

  return found->second;
}

void ToolAuditBridge::record_success(const std::string& detail_ref) {
  last_error_code_.reset();
  last_detail_ref_ = detail_ref;
}

void ToolAuditBridge::record_failure(
    std::optional<contracts::ResultCode> result_code,
    const std::string& detail_ref) {
  ++emit_failures_;
  last_error_code_ = result_code;
  last_detail_ref_ = detail_ref;
}

}  // namespace dasall::tools::ops
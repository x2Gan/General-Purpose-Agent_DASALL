#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "audit/AuditExporterTypes.h"
#include "audit/IAuditLogger.h"
#include "audit/AuditTypes.h"
#include "logging/LogTypes.h"
#include "checkpoint/RecoveryOutcomeGuards.h"
#include "task/WorkerTask.h"
#include "task/WorkerTaskGuards.h"
#include "tool/ToolResultGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasRequestIdMember = requires {
  &T::request_id;
};

template <typename T>
concept HasSessionIdMember = requires {
  &T::session_id;
};

template <typename T>
concept HasTraceIdMember = requires {
  &T::trace_id;
};

template <typename T>
concept HasToolResultMember = requires {
  &T::tool_result;
};

template <typename T>
concept HasRecoveryOutcomeMember = requires {
  &T::recovery_outcome;
};

template <typename T>
concept HasWorkerTaskMember = requires {
  &T::worker_task;
};

template <typename T>
concept HasGoalIdMember = requires {
  &T::goal_id;
};

template <typename T>
concept HasCheckpointRefMember = requires {
  &T::checkpoint_ref;
};

template <typename T>
concept HasGlobalFsmStateMember = requires {
  &T::global_fsm_state;
};

template <typename T>
concept HasParentTaskIdMember = requires {
  &T::parent_task_id;
};

template <typename T>
concept HasLeaseIdMember = requires {
  &T::lease_id;
};

template <typename T>
concept HasWorkerTypeMember = requires {
  &T::worker_type;
};

template <typename T>
concept HasOpaqueSelectorMember = requires {
  &T::opaque_selector;
};

void test_audit_event_accepts_tool_result_boundary_reference() {
  using dasall::contracts::ToolResult;
  using dasall::contracts::validate_tool_result_field_rules;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const ToolResult result{
      .request_id = std::string("req-001"),
      .tool_call_id = std::string("tool-call-001"),
      .tool_name = std::string("shell"),
      .success = true,
      .payload = std::string("{\"status\":\"ok\"}"),
      .error = std::nullopt,
      .side_effects = std::vector<std::string>{"wrote_file"},
      .completed_at = 12345,
      .duration_ms = 200,
      .goal_id = std::nullopt,
      .worker_task_id = std::string("task-001"),
      .tags = std::vector<std::string>{"audit"},
  };

  const auto result_guard = validate_tool_result_field_rules(result);
  assert_true(result_guard.ok,
              "ToolResult must pass its frozen contract guards before AuditEvent can reference it");

  const AuditEvent event{
      .event_id = std::string("audit-event-010"),
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell"),
      .outcome = AuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = AuditEvidenceKind::ToolResult,
          .ref = *result.tool_call_id,
      },
      .side_effects = {"wrote_file"},
      .timestamp = 1711785601000,
  };

  assert_true(event.has_required_fields(),
              "AuditEvent should keep tool evidence as a plain reference, not an embedded contract object");
  assert_true(event.references_contract_boundary(),
              "AuditEvent should admit ToolResult references without expanding contract semantics");
}

void test_audit_event_accepts_recovery_outcome_boundary_reference() {
  using dasall::contracts::RecoveryOutcome;
  using dasall::contracts::validate_recovery_outcome_field_rules;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const RecoveryOutcome outcome{
      .executed_action = std::string("rollback"),
      .final_runtime_state = std::string("degraded"),
      .updated_retry_count = 1,
      .checkpoint_ref = std::string("checkpoint-001"),
      .compensation_result_ref = std::string("comp-001"),
      .rejection_reason = std::nullopt,
      .escalation_reason = std::string("manual approval required"),
  };

  const auto outcome_guard = validate_recovery_outcome_field_rules(outcome);
  assert_true(outcome_guard.ok,
              "RecoveryOutcome must pass its frozen contract guards before AuditEvent can reference it");

  const AuditEvent event{
      .event_id = std::string("audit-event-011"),
      .action = std::string("ota.rollback"),
      .actor = std::string("recovery_manager"),
      .target = std::string("deployment-slot-a"),
      .outcome = AuditOutcome::Escalated,
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = *outcome.checkpoint_ref,
      },
      .side_effects = {"rollback_requested"},
      .timestamp = 1711785601100,
  };

  assert_true(event.has_required_fields(),
              "AuditEvent should keep recovery evidence as a stable reference string");
  assert_true(event.references_contract_boundary(),
              "AuditEvent should admit RecoveryOutcome references without importing recovery control fields");
}

void test_audit_event_accepts_worker_task_boundary_reference_without_embedding_task_object() {
  using dasall::contracts::WorkerTask;
  using dasall::contracts::validate_worker_task_field_rules;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditEvent{}.event_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditEvent{}.timestamp), std::int64_t>);
  static_assert(!HasRequestIdMember<AuditEvent>);
  static_assert(!HasSessionIdMember<AuditEvent>);
  static_assert(!HasTraceIdMember<AuditEvent>);
  static_assert(!HasToolResultMember<AuditEvent>);
  static_assert(!HasRecoveryOutcomeMember<AuditEvent>);
  static_assert(!HasWorkerTaskMember<AuditEvent>);

  const WorkerTask worker_task{
      .task_id = std::string("task-001"),
      .parent_task_id = std::string("parent-task-001"),
      .lease_id = std::string("lease-001"),
      .worker_type = std::string("tool-worker"),
      .allowed_tools = std::vector<std::string>{"shell"},
      .timeout_ms = 5000,
      .idempotency_key = std::string("idem-001"),
  };

  const auto worker_task_guard = validate_worker_task_field_rules(worker_task);
  assert_true(worker_task_guard.ok,
              "WorkerTask must pass its frozen contract guards before AuditEvent can reference it");

  const AuditEvent event{
      .event_id = std::string("audit-event-012"),
      .action = std::string("worker.dispatch"),
      .actor = std::string("multi_agent_coordinator"),
      .target = std::string("tool-worker"),
      .outcome = AuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = AuditEvidenceKind::WorkerTask,
          .ref = *worker_task.task_id,
      },
      .side_effects = {"worker_scheduled"},
      .timestamp = 1711785601200,
  };

  assert_true(event.has_required_fields(),
              "AuditEvent should keep worker-task evidence as an id reference instead of importing worker control state");
  assert_true(event.references_contract_boundary(),
              "AuditEvent should admit WorkerTask references without embedding task-domain structures");
}

void test_audit_event_rejects_unspecified_evidence_boundary() {
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  const AuditEvent event{
      .event_id = std::string("audit-event-013"),
      .action = std::string("tool.execute"),
      .actor = std::string("runtime"),
      .target = std::string("shell"),
      .outcome = AuditOutcome::Failed,
      .evidence_ref = {},
      .side_effects = {"wrote_file"},
      .timestamp = 1711785601300,
  };

  assert_true(!event.references_contract_boundary(),
              "unspecified evidence refs should fail the contract-boundary admission guard");
  assert_true(!event.has_required_fields(),
              "AuditEvent must not admit empty evidence refs for high-risk audit records");
}

void test_audit_context_keeps_correlation_fields_as_non_optional_strings() {
  using dasall::infra::AuditContext;
  using dasall::infra::kAuditContextUnknown;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditContext{}.request_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditContext{}.session_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditContext{}.trace_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditContext{}.task_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditContext{}.parent_task_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditContext{}.lease_id), std::string>);
  static_assert(std::is_same_v<decltype(AuditContext{}.worker_type), std::string>);
  static_assert(!HasGoalIdMember<AuditContext>);
  static_assert(!HasCheckpointRefMember<AuditContext>);
  static_assert(!HasGlobalFsmStateMember<AuditContext>);

  const AuditContext context{};

  assert_true(context.uses_unknown_defaults(),
              "AuditContext should default to unknown placeholders instead of optional null semantics");
  assert_true(context.request_id == kAuditContextUnknown &&
                  context.parent_task_id == kAuditContextUnknown &&
                  context.worker_type == kAuditContextUnknown,
              "AuditContext should keep all missing correlation anchors pinned to the frozen unknown placeholder");
}

void test_audit_context_rejects_empty_strings_when_unknown_semantics_are_bypassed() {
  using dasall::infra::AuditContext;
  using dasall::tests::support::assert_true;

  const AuditContext invalid_context{
      .request_id = std::string("req-001"),
      .session_id = std::string("session-001"),
      .trace_id = std::string("trace-001"),
      .task_id = std::string(),
      .parent_task_id = std::string("parent-task-001"),
      .lease_id = std::string("lease-001"),
      .worker_type = std::string("tool-worker"),
  };

  assert_true(!invalid_context.has_non_empty_fields(),
              "AuditContext should reject empty-string anchors when callers bypass the frozen unknown placeholder semantics");
}

void test_audit_logger_interface_uses_frozen_audit_boundary_objects_only() {
  using dasall::infra::AuditContext;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditWriteOutcome;
  using dasall::infra::ExportQuery;
  using dasall::infra::ExportResult;
  using dasall::infra::audit::IAuditLogger;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IAuditLogger::write_audit),
                               AuditWriteOutcome (IAuditLogger::*)(const AuditEvent&, const AuditContext&)>);
  static_assert(std::is_same_v<decltype(&IAuditLogger::export_audit),
                               ExportResult (IAuditLogger::*)(const ExportQuery&)>);
  static_assert(std::is_same_v<decltype(ExportResult{}.records), std::vector<AuditEvent>>);
  static_assert(!HasOpaqueSelectorMember<ExportQuery>);

  const AuditContext context{};
  const ExportQuery query{
      .start_ts = 1711785600000,
      .end_ts = 1711785609000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };

  assert_true(context.uses_unknown_defaults(),
              "IAuditLogger should accept the frozen AuditContext unknown-default semantics without reintroducing nullable placeholders");
  assert_true(query.has_ordered_window() && !query.requests_page_resume(),
              "IAuditLogger export input should stay bound to the frozen ExportQuery time-window shape rather than the retired opaque selector placeholder");
}

  void test_logging_audit_aliases_keep_multi_agent_context_outside_event_top_level() {
    using LoggingAuditContext = dasall::infra::logging::AuditContext;
    using LoggingAuditEvent = dasall::infra::logging::AuditEvent;
    using LoggingAuditEvidenceKind = dasall::infra::logging::AuditEvidenceKind;
    using LoggingAuditOutcome = dasall::infra::logging::AuditOutcome;
    using dasall::tests::support::assert_true;

    static_assert(std::is_same_v<LoggingAuditEvent, dasall::infra::AuditEvent>);
    static_assert(std::is_same_v<LoggingAuditContext, dasall::infra::AuditContext>);
    static_assert(!HasParentTaskIdMember<LoggingAuditEvent>);
    static_assert(!HasLeaseIdMember<LoggingAuditEvent>);
    static_assert(!HasWorkerTypeMember<LoggingAuditEvent>);
    static_assert(HasParentTaskIdMember<LoggingAuditContext>);
    static_assert(HasLeaseIdMember<LoggingAuditContext>);
    static_assert(HasWorkerTypeMember<LoggingAuditContext>);

    const LoggingAuditEvent event{
      .event_id = std::string("audit-event-logging-ctx-005"),
      .action = std::string("worker.dispatch"),
      .actor = std::string("multi_agent_coordinator"),
      .target = std::string("tool-worker"),
      .outcome = LoggingAuditOutcome::Succeeded,
      .evidence_ref = {
        .kind = LoggingAuditEvidenceKind::WorkerTask,
        .ref = std::string("task-ctx-005"),
      },
      .side_effects = {"worker_scheduled"},
      .timestamp = 1711785601400,
    };

    const LoggingAuditContext context{
      .request_id = std::string("req-ctx-005"),
      .session_id = std::string("session-ctx-005"),
      .trace_id = std::string("trace-ctx-005"),
      .task_id = std::string("task-ctx-005"),
      .parent_task_id = std::string("parent-task-ctx-005"),
      .lease_id = std::string("lease-ctx-005"),
      .worker_type = std::string("tool-worker"),
    };

    assert_true(event.has_required_fields(),
          "logging::AuditEvent should keep only the frozen audit fact fields at top level");
    assert_true(context.has_non_empty_fields() &&
            context.parent_task_id == "parent-task-ctx-005" &&
            context.lease_id == "lease-ctx-005" &&
            context.worker_type == "tool-worker",
          "logging::AuditContext should retain ADR-008 multi-agent chain identifiers without pushing them into AuditEvent top-level fields");
  }

}  // namespace

int main() {
  try {
    test_audit_event_accepts_tool_result_boundary_reference();
    test_audit_event_accepts_recovery_outcome_boundary_reference();
    test_audit_event_accepts_worker_task_boundary_reference_without_embedding_task_object();
    test_audit_event_rejects_unspecified_evidence_boundary();
    test_audit_context_keeps_correlation_fields_as_non_optional_strings();
    test_audit_context_rejects_empty_strings_when_unknown_semantics_are_bypassed();
    test_audit_logger_interface_uses_frozen_audit_boundary_objects_only();
    test_logging_audit_aliases_keep_multi_agent_context_outside_event_top_level();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
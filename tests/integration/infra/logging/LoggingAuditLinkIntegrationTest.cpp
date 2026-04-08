#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "logging/AuditLinkAdapter.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

void test_logging_audit_link_routes_high_risk_events_to_audit_sink() {
  using dasall::infra::InfraContext;
  using dasall::infra::logging::AuditEvidenceKind;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));

  assert_true(facade.init(InfraContext{
                  .request_id = std::string("req-int-audit-001"),
                  .session_id = std::string("session-int-audit-001"),
                  .trace_id = std::string("trace-int-audit-001"),
                  .task_id = std::string("task-int-audit-001"),
                  .parent_task_id = std::string("parent-int-audit-001"),
                  .lease_id = std::string("lease-int-audit-001"),
              })
                  .ok,
              "logging audit integration should initialize the facade before routing a high-risk event");

  LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("audit integration smoke"),
      .attrs = {{"event_kind", "high_risk"}},
      .ts = 1712217600101,
  };
  const AuditRef audit_ref{
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("recovery-int-001"),
      },
      .trace_id = std::string("trace-int-audit-ref-001"),
      .task_id = std::string("task-int-audit-ref-001"),
  };

  const auto attach_result = adapter.attach_audit_ref(event, audit_ref);
  assert_true(attach_result.ok,
              "logging audit integration should attach a complete audit ref before dispatch");

  const auto log_result = facade.log(event);
  assert_true(log_result.ok,
              "logging audit integration should dispatch the linked high-risk event successfully");
  assert_true(dispatcher_ptr->last_route() == SinkRoute::Audit,
              "logging audit integration should route linked high-risk events to the audit sink");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->dispatched_record_count(SinkRoute::Audit)),
               "logging audit integration should count one audit-routed record");
  assert_true(dispatcher_ptr->last_record().event.attrs.at("evidence_ref") ==
                  "recovery-int-001",
              "logging audit integration should preserve evidence_ref on the routed record");
  assert_true(facade.last_dispatched_event().attrs.at("request_id") ==
                  "req-int-audit-001",
              "logging audit integration should keep context enrichment alongside audit attrs");
}

void test_logging_audit_link_rejects_incomplete_refs_without_dispatching_side_effects() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::AuditEvidenceKind;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  LogEvent event{
      .level = LogLevel::Fatal,
      .module = std::string("runtime"),
      .message = std::string("audit integration rejection"),
      .attrs = {{"event_kind", "high_risk_missing_ref"}},
      .ts = 1712217600102,
  };
  const AuditRef incomplete_ref{
      .evidence_ref = {
          .kind = AuditEvidenceKind::WorkerTask,
          .ref = std::string("worker-task-int-001"),
      },
      .trace_id = std::string("unknown"),
      .task_id = std::string("unknown"),
  };

  const auto attach_result = adapter.attach_audit_ref(event, incomplete_ref);
  assert_true(!attach_result.ok,
              "logging audit integration should reject incomplete audit refs for high-risk events");
  assert_true(attach_result.references_only_contract_error_types(),
              "logging audit integration rejection should stay within contracts error types");
  assert_true(attach_result.result_code == ResultCode::ValidationFieldMissing,
              "logging audit integration should map incomplete refs to ValidationFieldMissing");
  assert_equal(1,
               static_cast<int>(adapter.link_failure_count()),
               "logging audit integration should record one link failure when a ref is incomplete");
  assert_true(!event.attrs.contains("audit_ref_pending"),
              "logging audit integration should not emit partial audit routing attrs on rejection");
}

}  // namespace

int main() {
  try {
    test_logging_audit_link_routes_high_risk_events_to_audit_sink();
    test_logging_audit_link_rejects_incomplete_refs_without_dispatching_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
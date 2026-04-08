#include <exception>
#include <iostream>
#include <string>

#include "logging/AuditLinkAdapter.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasEvidenceRefMember = requires {
  &T::evidence_ref;
};

template <typename T>
concept HasAuditTraceIdMember = requires {
  &T::audit_trace_id;
};

void test_audit_link_adapter_keeps_audit_fields_inside_attr_surface() {
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::tests::support::assert_true;

  static_assert(!HasEvidenceRefMember<LogEvent>);
  static_assert(!HasAuditTraceIdMember<LogEvent>);

  AuditLinkAdapter adapter;
  LogEvent event{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("audit boundary completeness"),
      .attrs = {{"request_id", "req-contract-009"}},
      .ts = 1711968609100,
  };
  const AuditRef audit_ref{
      .evidence_ref = {
          .kind = dasall::infra::logging::AuditEvidenceKind::WorkerTask,
          .ref = std::string("worker-task-009"),
      },
      .trace_id = std::string("trace-contract-009"),
      .task_id = std::string("task-contract-009"),
  };

  const auto result = adapter.attach_audit_ref(event, audit_ref);
  assert_true(result.ok,
              "audit link adapter should keep correlation data inside the frozen attrs boundary");
  assert_true(event.attrs.at("audit_ref_pending") == "true",
              "audit_ref_pending should stay inside attrs as the routing hint");
  assert_true(event.attrs.at("evidence_ref") == "worker-task-009",
              "evidence_ref should remain an attr instead of a new top-level log field");
  assert_true(event.attrs.at("evidence_kind") == "worker_task",
              "evidence kind should remain serializable through the attrs surface");
  assert_true(event.attrs.at("audit_trace_id") == "trace-contract-009",
              "audit trace correlation should remain inside attrs");
  assert_true(event.attrs.at("audit_task_id") == "task-contract-009",
              "audit task correlation should remain inside attrs");
}

void test_audit_link_adapter_completeness_drives_audit_route_selection() {
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  SinkDispatcher dispatcher;
  LogEvent event{
      .level = dasall::infra::logging::LogLevel::Fatal,
      .module = std::string("runtime"),
      .message = std::string("audit routing completeness"),
      .attrs = {{"request_id", "req-contract-009-route"}},
      .ts = 1711968609101,
  };
  const AuditRef audit_ref{
      .evidence_ref = {
          .kind = dasall::infra::logging::AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("recovery-009"),
      },
      .trace_id = std::string("trace-contract-009-route"),
      .task_id = std::string("task-contract-009-route"),
  };

  assert_true(adapter.attach_audit_ref(event, audit_ref).ok,
              "complete audit refs should attach successfully before routing");
  assert_true(dispatcher.dispatch(event).ok,
              "dispatcher should accept audit-linked events at the boundary");
  assert_true(dispatcher.last_route() == SinkRoute::Audit,
              "complete audit-link attrs should be sufficient to select the audit route");
}

}  // namespace

int main() {
  try {
    test_audit_link_adapter_keeps_audit_fields_inside_attr_surface();
    test_audit_link_adapter_completeness_drives_audit_route_selection();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
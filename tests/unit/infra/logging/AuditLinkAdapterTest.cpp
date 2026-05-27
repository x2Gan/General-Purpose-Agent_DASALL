#include <exception>
#include <iostream>
#include <string>

#include "logging/AuditLinkAdapter.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::logging::AuditRef make_audit_ref() {
  return dasall::infra::logging::AuditRef{
      .evidence_ref = {
          .kind = dasall::infra::logging::AuditEvidenceKind::ToolResult,
          .ref = std::string("tool-call-009"),
      },
      .trace_id = std::string("trace-log-009"),
      .task_id = std::string("task-log-009"),
  };
}

dasall::infra::logging::LogEvent make_high_risk_event() {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("audit link adapter high-risk path"),
      .attrs = {
          {"request_id", "req-log-009"},
          {"event_kind", "high_risk"},
      },
      .ts = 1711968609000,
  };
}

void test_audit_link_adapter_attaches_evidence_to_high_risk_events() {
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  auto event = make_high_risk_event();

  const auto result = adapter.attach_audit_ref(event, make_audit_ref());
  assert_true(result.ok,
              "audit link adapter should attach evidence data to high-risk logs");
  assert_true(event.attrs.at("audit_ref_pending") == "true",
              "audit link adapter should mark the log for audit routing");
  assert_true(event.attrs.at("evidence_ref") == "tool-call-009",
              "audit link adapter should expose the evidence_ref attr for downstream routing");
  assert_true(event.attrs.at("audit_trace_id") == "trace-log-009",
              "audit link adapter should preserve trace correlation on the linked event");
  assert_true(event.attrs.at("audit_task_id") == "task-log-009",
              "audit link adapter should preserve task correlation on the linked event");

  SinkDispatcher dispatcher;
  const auto dispatch_result = dispatcher.dispatch(event);
  assert_true(dispatch_result.ok,
              "sink dispatcher should accept audit-linked events after the adapter mutates attrs");
  assert_true(dispatcher.last_route() == SinkRoute::Audit,
              "audit-linked high-risk events should be routed to the audit sink path");
}

void test_audit_link_adapter_reports_failures_for_incomplete_refs() {
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  auto event = make_high_risk_event();

  const auto result = adapter.attach_audit_ref(event, dasall::infra::logging::AuditRef{});
  assert_true(!result.ok,
              "audit link adapter should reject incomplete audit references on high-risk logs");
  assert_true(result.references_only_contract_error_types(),
              "audit link adapter failures should stay inside contracts error types");
  assert_true(adapter.link_failure_count() == 1,
              "audit link adapter should expose a monotonic link-failure counter");
  assert_true(!adapter.last_failure_reason().empty(),
              "audit link adapter should retain the last failure reason for alerting outlets");
}

}  // namespace

int main() {
  try {
    test_audit_link_adapter_attaches_evidence_to_high_risk_events();
    test_audit_link_adapter_reports_failures_for_incomplete_refs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
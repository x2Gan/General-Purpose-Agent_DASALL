#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "audit/IAuditLogger.h"
#include "logging/AuditLinkAdapter.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);

    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.pop_front();
      return outcome;
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool contains_fragment(const std::vector<std::string>& values,
                                     std::string_view fragment) {
  for (const auto& value : values) {
    if (value.find(fragment) != std::string::npos) {
      return true;
    }
  }

  return false;
}

dasall::infra::logging::AuditRef make_audit_ref() {
  return dasall::infra::logging::AuditRef{
      .evidence_ref = {
          .kind = dasall::infra::logging::AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("recovery-unit-008"),
      },
      .trace_id = std::string("trace-unit-audit-008"),
      .task_id = std::string("task-unit-audit-008"),
  };
}

dasall::infra::logging::LogEvent make_high_risk_event() {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("unit persistence authorization=secret-unit-008"),
      .attrs = {
          {"event_kind", "high_risk"},
          {"authorization", "Bearer secret-unit-008"},
      },
      .ts = 1712217601101,
  };
}

void test_audit_link_persistence_emits_one_audit_record_without_payload_duplication() {
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  auto audit_logger = std::make_shared<ScriptedAuditLogger>();
  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));
  facade.attach_audit_logger(audit_logger);

  assert_true(facade.init(dasall::infra::InfraContext{}).ok,
              "audit link persistence unit should initialize the logging facade before emission");

  auto event = make_high_risk_event();
  assert_true(adapter.attach_audit_ref(event, make_audit_ref()).ok,
              "audit link persistence unit should attach a complete audit ref before emission");

  const auto log_result = facade.log(event);
  assert_true(log_result.ok,
              "audit link persistence unit should emit a high-risk event when audit handoff succeeds");
  assert_true(dispatcher_ptr->last_route() == SinkRoute::Audit,
              "audit link persistence unit should keep the ordinary log on the audit route");
  assert_equal(1,
               static_cast<int>(audit_logger->events.size()),
               "audit link persistence unit should persist one correlated audit event");

  const auto& audit_event = audit_logger->events.front();
  assert_true(audit_event.action == "logging.audit_route",
              "audit link persistence unit should use the frozen logging.audit_route action");
  assert_true(audit_event.target == "log_event:runtime:high_risk",
              "audit link persistence unit should use the frozen route target namespace");
  assert_true(!contains_fragment(audit_event.side_effects, "secret-unit-008") &&
                  !contains_fragment(audit_event.side_effects, "authorization"),
              "audit link persistence unit should not duplicate privacy-sensitive payload into audit side effects");
  assert_true(dispatcher_ptr->last_record().event.message.find("secret-unit-008") ==
                  std::string::npos,
              "audit link persistence unit should keep the ordinary log redacted after audit handoff");
}

void test_audit_link_persistence_fails_closed_when_audit_logger_rejects_write() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  auto audit_logger = std::make_shared<ScriptedAuditLogger>();
  audit_logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = ResultCode::ProviderTimeout,
  });

  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));
  facade.attach_audit_logger(audit_logger);

  assert_true(facade.init(dasall::infra::InfraContext{}).ok,
              "audit link persistence unit should initialize the logging facade before failure injection");

  auto event = make_high_risk_event();
  assert_true(adapter.attach_audit_ref(event, make_audit_ref()).ok,
              "audit link persistence unit should attach a complete audit ref before the failure path");

  const auto log_result = facade.log(event);
  assert_true(!log_result.ok,
              "audit link persistence unit should fail closed when audit persistence is rejected");
  assert_true(log_result.result_code == ResultCode::ProviderTimeout,
              "audit link persistence unit should surface the audit logger failure code");
  assert_true(!dispatcher_ptr->has_last_record(),
              "audit link persistence unit should not dispatch the ordinary log after audit persistence failure");
}

}  // namespace

int main() {
  try {
    test_audit_link_persistence_emits_one_audit_record_without_payload_duplication();
    test_audit_link_persistence_fails_closed_when_audit_logger_rejects_write();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "audit/IAuditLogger.h"
#include "logging/AuditLinkAdapter.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasActorMember = requires {
  &T::actor;
};

template <typename T>
concept HasActionMember = requires {
  &T::action;
};

template <typename T>
concept HasTargetMember = requires {
  &T::target;
};

template <typename T>
concept HasOutcomeMember = requires {
  &T::outcome;
};

template <typename T>
concept HasSideEffectsMember = requires {
  &T::side_effects;
};

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    last_event = event;
    last_context = context;
    write_count += 1;
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

  std::size_t write_count = 0;
  dasall::infra::AuditEvent last_event{};
  dasall::infra::AuditContext last_context{};
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

void test_audit_log_correlation_keeps_audit_payload_outside_log_event_boundary() {
  using dasall::infra::logging::AuditEvidenceKind;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::tests::support::assert_true;

  static_assert(!HasActorMember<LogEvent>);
  static_assert(!HasActionMember<LogEvent>);
  static_assert(!HasTargetMember<LogEvent>);
  static_assert(!HasOutcomeMember<LogEvent>);
  static_assert(!HasSideEffectsMember<LogEvent>);

  AuditLinkAdapter adapter;
  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));
  facade.attach_audit_logger(audit_logger);

  assert_true(facade.init(dasall::infra::InfraContext{}).ok,
              "audit log correlation contract should initialize the logging facade before correlation checks");

  LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("contract correlation authorization=secret-contract-008"),
      .attrs = {
          {"event_kind", "high_risk"},
          {"authorization", "Bearer secret-contract-008"},
      },
      .ts = 1712217602101,
  };

  const AuditRef audit_ref{
      .evidence_ref = {
          .kind = AuditEvidenceKind::WorkerTask,
          .ref = std::string("worker-task-contract-008"),
      },
      .trace_id = std::string("trace-contract-audit-008"),
      .task_id = std::string("task-contract-audit-008"),
  };

  assert_true(adapter.attach_audit_ref(event, audit_ref).ok,
              "audit log correlation contract should accept a complete audit ref before dispatch");
  assert_true(facade.log(event).ok,
              "audit log correlation contract should allow a correlated high-risk event when both surfaces are available");
  assert_true(audit_logger->write_count == 1,
              "audit log correlation contract should persist exactly one correlated audit event");
  assert_true(dispatcher_ptr->last_record().event.attrs.at("evidence_ref") ==
                  audit_logger->last_event.evidence_ref.ref,
              "audit log correlation contract should correlate ordinary log and audit persistence via the same evidence_ref");
  assert_true(audit_logger->last_context.trace_id == "trace-contract-audit-008" &&
                  audit_logger->last_context.task_id == "task-contract-audit-008",
              "audit log correlation contract should preserve the frozen trace/task anchors in AuditContext");
  assert_true(dispatcher_ptr->last_record().event.attrs.contains("audit_ref_pending") &&
                  dispatcher_ptr->last_record().event.attrs.contains("evidence_kind") &&
                  dispatcher_ptr->last_record().event.attrs.contains("audit_trace_id") &&
                  dispatcher_ptr->last_record().event.attrs.contains("audit_task_id"),
              "audit log correlation contract should keep only the frozen audit correlation attrs on the ordinary log surface");
  assert_true(!dispatcher_ptr->last_record().event.attrs.contains("actor") &&
                  !dispatcher_ptr->last_record().event.attrs.contains("action") &&
                  !dispatcher_ptr->last_record().event.attrs.contains("target") &&
                  !dispatcher_ptr->last_record().event.attrs.contains("outcome") &&
                  !dispatcher_ptr->last_record().event.attrs.contains("side_effects"),
              "audit log correlation contract should not project audit payload fields back onto LogEvent attrs");
  assert_true(dispatcher_ptr->last_record().event.message.find("secret-contract-008") ==
                  std::string::npos,
              "audit log correlation contract should keep secrets out of the persisted ordinary log record");
  assert_true(audit_logger->last_event.target.find("secret-contract-008") == std::string::npos &&
                  !contains_fragment(audit_logger->last_event.side_effects,
                                     "secret-contract-008") &&
                  !contains_fragment(audit_logger->last_event.side_effects,
                                     "authorization"),
              "audit log correlation contract should keep privacy-sensitive payload out of the correlated audit record");
}

}  // namespace

int main() {
  try {
    test_audit_log_correlation_keeps_audit_payload_outside_log_event_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
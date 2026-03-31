#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "audit/IAuditLogger.h"
#include "logging/IAuditLinkAdapter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::AuditContext make_context() {
  return dasall::infra::AuditContext{};
}

dasall::infra::ExportQuery make_query() {
  return dasall::infra::ExportQuery{
      .start_ts = 1711785602000,
      .end_ts = 1711785602600,
      .actor = std::string("runtime"),
      .action = std::string("tool.execute"),
      .target = std::string("shell"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .page_token = std::string(),
  };
}

class NullAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    if (!event.has_required_fields() || !event.side_effects_are_serializable() ||
        !context.has_non_empty_fields()) {
      return dasall::infra::AuditWriteOutcome{
          .accepted = false,
          .persisted = false,
          .fallback_used = false,
          .error_code = dasall::contracts::ResultCode::ValidationFieldMissing,
      };
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
      .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery& query) override {
    if (!query.has_ordered_window()) {
      return dasall::infra::ExportResult{};
    }

    return dasall::infra::ExportResult{
        .records = {dasall::infra::AuditEvent{
      .event_id = std::string("audit-event-export-001"),
            .action = std::string("tool.execute"),
            .actor = std::string("runtime"),
            .target = std::string("shell"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
            .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                             .ref = std::string("tool-call-001")},
            .side_effects = {"wrote_file"},
      .timestamp = 1711785602000,
        }},
        .next_page_token = std::string(),
        .truncated = false,
        .checksum = std::string("null-audit-export"),
    };
  }
};

class NullAuditLinkAdapter final : public dasall::infra::logging::IAuditLinkAdapter {
 public:
  dasall::infra::LogWriteResult attach_audit_ref(
      dasall::infra::LogEvent& event,
      const dasall::infra::logging::AuditRef& audit_ref) override {
    static_cast<void>(&audit_ref);
    event.attrs.insert_or_assign("audit_ref_pending", "true");
    return dasall::infra::LogWriteResult::success();
  }

  void report_link_failure(std::string_view reason) override {
    last_failure_reason_ = std::string(reason);
  }

  [[nodiscard]] const std::string& last_failure_reason() const {
    return last_failure_reason_;
  }

 private:
  std::string last_failure_reason_;
};

void test_audit_link_adapter_freezes_placeholder_linking_interface() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogWriteResult;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::IAuditLinkAdapter;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IAuditLinkAdapter::attach_audit_ref),
                               LogWriteResult (IAuditLinkAdapter::*)(LogEvent&, const AuditRef&)>);
  static_assert(std::is_same_v<decltype(&IAuditLinkAdapter::report_link_failure),
                               void (IAuditLinkAdapter::*)(std::string_view)>);

  NullAuditLinkAdapter adapter;
  adapter.report_link_failure("missing audit ref placeholder");

  assert_true(adapter.last_failure_reason() == "missing audit ref placeholder",
              "IAuditLinkAdapter should expose an explicit failure-reporting outlet while AuditRef remains a placeholder boundary");
}

void test_audit_logger_interface_freezes_write_and_export_signatures() {
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditContext;
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::AuditWriteOutcome;
  using dasall::infra::ExportQuery;
  using dasall::infra::ExportResult;
  using dasall::infra::audit::IAuditLogger;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IAuditLogger::write_audit),
                               AuditWriteOutcome (IAuditLogger::*)(const AuditEvent&, const AuditContext&)>);
  static_assert(std::is_same_v<decltype(&IAuditLogger::export_audit),
                               ExportResult (IAuditLogger::*)(const ExportQuery&)>);

  NullAuditLogger logger;

  const AuditEvent event{
      .event_id = std::string("audit-event-020"),
      .action = std::string("policy.patch"),
      .actor = std::string("runtime"),
      .target = std::string("policy-bundle-v2"),
      .outcome = AuditOutcome::Succeeded,
      .evidence_ref = {.kind = AuditEvidenceKind::ToolResult,
                       .ref = std::string("tool-call-002")},
      .side_effects = {"policy_reloaded"},
      .timestamp = 1711785602100,
  };

  const auto write_result = logger.write_audit(event, make_context());
  assert_true(write_result.is_success(),
              "IAuditLogger interface should accept a valid AuditEvent/AuditContext pair after the 6.6 freeze");

  const auto export_result = logger.export_audit(make_query());
  assert_true(export_result.records.size() == 1,
              "IAuditLogger export should keep AuditEvent as the retained export payload boundary");
  assert_true(export_result.has_checksum() && export_result.is_complete_page(),
              "IAuditLogger export should surface the frozen ExportResult pagination and checksum fields");
}

void test_audit_logger_interface_rejects_invalid_event_or_context_on_write_path() {
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditContext;
  using dasall::infra::AuditOutcome;
  using dasall::tests::support::assert_true;

  NullAuditLogger logger;

  const AuditEvent invalid_event{
      .event_id = std::string("audit-event-021"),
      .action = std::string(),
      .actor = std::string("runtime"),
      .target = std::string("deployment"),
      .outcome = AuditOutcome::Failed,
      .evidence_ref = {},
      .side_effects = {"rollback_requested"},
      .timestamp = 1711785602200,
  };

  const AuditContext invalid_context{
      .request_id = std::string("req-001"),
      .session_id = std::string("session-001"),
      .trace_id = std::string("trace-001"),
      .task_id = std::string(),
      .parent_task_id = std::string("parent-task-001"),
      .lease_id = std::string("lease-001"),
      .worker_type = std::string("runtime"),
  };

  const auto write_result = logger.write_audit(invalid_event, invalid_context);
  assert_true(write_result.has_consistent_state() && write_result.is_failure(),
              "IAuditLogger write path should expose invalid event/context input as a consistent AuditWriteOutcome failure");
  assert_true(write_result.error_code == dasall::contracts::ResultCode::ValidationFieldMissing,
              "IAuditLogger write validation failures should stay mapped to existing contracts result codes");
}

}  // namespace

int main() {
  try {
    test_audit_link_adapter_freezes_placeholder_linking_interface();
    test_audit_logger_interface_freezes_write_and_export_signatures();
    test_audit_logger_interface_rejects_invalid_event_or_context_on_write_path();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
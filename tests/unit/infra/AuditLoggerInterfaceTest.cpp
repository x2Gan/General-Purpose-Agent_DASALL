#include <exception>
#include <iostream>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "audit/IAuditHealthProbe.h"
#include "audit/IAuditLogger.h"
#include "audit/IAuditRetention.h"
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

class NullAuditHealthProbe final : public dasall::infra::audit::IAuditHealthProbe {
 public:
  explicit NullAuditHealthProbe(dasall::infra::AuditHealthStatus snapshot)
      : snapshot_(std::move(snapshot)) {}

  [[nodiscard]] dasall::infra::AuditHealthStatus evaluate() const override {
    return snapshot_;
  }

 private:
  dasall::infra::AuditHealthStatus snapshot_;
};

class NullAuditRetention final : public dasall::infra::audit::IAuditRetention {
 public:
  explicit NullAuditRetention(dasall::infra::RetentionOutcome outcome)
      : outcome_(std::move(outcome)) {}

  [[nodiscard]] dasall::infra::RetentionOutcome apply_retention(
      std::int64_t now_ts) override {
    last_now_ts_ = now_ts;
    return outcome_;
  }

  [[nodiscard]] std::int64_t last_now_ts() const {
    return last_now_ts_;
  }

 private:
  dasall::infra::RetentionOutcome outcome_;
  std::int64_t last_now_ts_ = 0;
};

class NullAuditLinkAdapter final : public dasall::infra::logging::IAuditLinkAdapter {
 public:
  dasall::infra::logging::LogWriteResult attach_audit_ref(
      dasall::infra::logging::LogEvent& event,
      const dasall::infra::logging::AuditRef& audit_ref) override {
    static_cast<void>(&audit_ref);
    event.attrs.insert_or_assign("audit_ref_pending", "true");
    return dasall::infra::logging::LogWriteResult::success();
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
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::IAuditLinkAdapter;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogWriteResult;
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

template <typename T>
concept HasProbeMethod = requires {
  &T::probe;
};

template <typename T>
concept HasRegisterProbeMethod = requires {
  &T::register_probe;
};

template <typename T>
concept HasArchiveMethod = requires {
  &T::archive;
};

template <typename T>
concept HasCleanupMethod = requires {
  &T::cleanup;
};

void test_audit_health_probe_interface_freezes_evaluate_signature() {
  using dasall::infra::AuditHealthState;
  using dasall::infra::AuditHealthStatus;
  using dasall::infra::audit::IAuditHealthProbe;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IAuditHealthProbe::evaluate),
                               AuditHealthStatus (IAuditHealthProbe::*)() const>);
  static_assert(std::is_abstract_v<IAuditHealthProbe>);

  NullAuditHealthProbe probe(AuditHealthStatus{
      .state = AuditHealthState::Ready,
      .last_failure_reason = std::string(),
      .detail_ref = std::string("diag://infra/audit/health/ready"),
      .error_code = std::nullopt,
      .sampled_at_unix_ms = 1712217600100,
      .fallback_active = false,
      .metrics_bridge_degraded = false,
  });

  const auto snapshot = probe.evaluate();
  assert_true(std::has_virtual_destructor_v<IAuditHealthProbe>,
              "IAuditHealthProbe should remain a pure virtual boundary with a virtual destructor");
  assert_true(snapshot.state == AuditHealthState::Ready && snapshot.has_consistent_state(),
              "IAuditHealthProbe should expose a read-only AuditHealthStatus snapshot with the frozen Ready semantics");
}

void test_audit_health_status_accepts_degraded_and_unavailable_snapshots() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditHealthState;
  using dasall::infra::AuditHealthStatus;
  using dasall::tests::support::assert_true;

  const AuditHealthStatus degraded_snapshot{
      .state = AuditHealthState::Degraded,
      .last_failure_reason = std::string("fallback_active"),
      .detail_ref = std::string("diag://infra/audit/health/degraded/fallback_active"),
      .error_code = std::nullopt,
      .sampled_at_unix_ms = 1712217600200,
      .fallback_active = true,
      .metrics_bridge_degraded = false,
  };
  const AuditHealthStatus unavailable_snapshot{
      .state = AuditHealthState::Unavailable,
      .last_failure_reason = std::string("service_stopped"),
      .detail_ref = std::string("diag://infra/audit/health/unavailable/service_stopped"),
      .error_code = ResultCode::RuntimeRetryExhausted,
      .sampled_at_unix_ms = 1712217600300,
      .fallback_active = false,
      .metrics_bridge_degraded = false,
  };

  assert_true(degraded_snapshot.has_consistent_state(),
              "AuditHealthStatus should admit a degraded snapshot when fallback remains active and observable");
  assert_true(unavailable_snapshot.has_consistent_state(),
              "AuditHealthStatus should admit an unavailable snapshot only when it carries error_code and failure evidence");
}

void test_audit_health_status_rejects_invalid_ready_and_reason_combinations() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditHealthState;
  using dasall::infra::AuditHealthStatus;
  using dasall::infra::audit::IAuditHealthProbe;
  using dasall::tests::support::assert_true;

  static_assert(!HasProbeMethod<IAuditHealthProbe>);
  static_assert(!HasRegisterProbeMethod<IAuditHealthProbe>);

  const AuditHealthStatus ready_with_failure_bits{
      .state = AuditHealthState::Ready,
      .last_failure_reason = std::string(),
      .detail_ref = std::string("diag://infra/audit/health/ready"),
      .error_code = ResultCode::RuntimeRetryExhausted,
      .sampled_at_unix_ms = 1712217600400,
      .fallback_active = false,
      .metrics_bridge_degraded = false,
  };
  const AuditHealthStatus degraded_with_unknown_reason{
      .state = AuditHealthState::Degraded,
      .last_failure_reason = std::string("freeform_reason"),
      .detail_ref = std::string("diag://infra/audit/health/degraded/freeform_reason"),
      .error_code = std::nullopt,
      .sampled_at_unix_ms = 1712217600500,
      .fallback_active = false,
      .metrics_bridge_degraded = false,
  };

  assert_true(!ready_with_failure_bits.has_consistent_state(),
              "AuditHealthStatus should reject Ready snapshots that still carry failure bits");
  assert_true(!degraded_with_unknown_reason.has_consistent_state(),
              "AuditHealthStatus should reject degraded snapshots that use non-frozen failure reasons");
}

  void test_audit_retention_interface_freezes_apply_signature_and_outcome_shape() {
    using dasall::infra::AuditArchiveAction;
    using dasall::infra::AuditCleanupEvidence;
    using dasall::infra::AuditCleanupTrigger;
    using dasall::infra::RetentionOutcome;
    using dasall::infra::audit::IAuditRetention;
    using dasall::tests::support::assert_true;

    static_assert(std::is_same_v<decltype(&IAuditRetention::apply_retention),
                   RetentionOutcome (IAuditRetention::*)(std::int64_t)>);
    static_assert(std::is_same_v<decltype(RetentionOutcome{}.archive_action),
                   std::optional<AuditArchiveAction>>);
    static_assert(std::is_same_v<decltype(RetentionOutcome{}.cleanup_evidence),
                   std::optional<AuditCleanupEvidence>>);
    static_assert(std::is_abstract_v<IAuditRetention>);
    static_assert(!HasArchiveMethod<IAuditRetention>);
    static_assert(!HasCleanupMethod<IAuditRetention>);

    const std::int64_t cutoff_ts = 1712304000000;
    NullAuditRetention retention(RetentionOutcome{
      .completed = true,
      .cutoff_ts = cutoff_ts,
      .scanned_records = 12,
      .archived_records = 12,
      .deleted_records = 12,
      .detail_ref = std::string("diag://infra/audit/retention/manual_cleanup"),
      .error_code = std::nullopt,
      .archive_action = AuditArchiveAction{
        .archive_ref = std::string("diag://infra/audit/retention/archive/batch-001"),
        .archived_records = 12,
        .archived_through_ts = cutoff_ts,
        .checksum = std::string("audit-retention-batch-001"),
      },
      .cleanup_evidence = AuditCleanupEvidence{
        .trigger = AuditCleanupTrigger::Manual,
        .cleanup_ref = std::string("diag://infra/audit/retention/cleanup/run-001"),
        .archive_ref = std::string("diag://infra/audit/retention/archive/batch-001"),
        .deleted_records = 12,
        .deleted_through_ts = cutoff_ts,
      },
    });

    const auto outcome = retention.apply_retention(cutoff_ts + 60000);
    assert_true(std::has_virtual_destructor_v<IAuditRetention>,
          "IAuditRetention should remain a pure virtual boundary with a virtual destructor");
    assert_true(retention.last_now_ts() == cutoff_ts + 60000 && outcome.is_success(),
          "IAuditRetention should keep retention execution behind a single apply_retention(now_ts) entrypoint and return a success outcome with aligned archive/cleanup evidence");
  }

  void test_retention_outcome_accepts_failure_and_rejects_cleanup_without_trace() {
    using dasall::contracts::ResultCode;
    using dasall::infra::RetentionOutcome;
    using dasall::tests::support::assert_true;

    const std::int64_t cutoff_ts = 1712307600000;
    const RetentionOutcome failure{
      .completed = false,
      .cutoff_ts = cutoff_ts,
      .scanned_records = 4,
      .archived_records = 0,
      .deleted_records = 0,
      .detail_ref = std::string("diag://infra/audit/retention/failure"),
      .error_code = ResultCode::RuntimeRetryExhausted,
      .archive_action = std::nullopt,
      .cleanup_evidence = std::nullopt,
    };
    const RetentionOutcome invalid_cleanup{
      .completed = true,
      .cutoff_ts = cutoff_ts,
      .scanned_records = 3,
      .archived_records = 0,
      .deleted_records = 1,
      .detail_ref = std::string("diag://infra/audit/retention/manual_cleanup"),
      .error_code = std::nullopt,
      .archive_action = std::nullopt,
      .cleanup_evidence = std::nullopt,
    };

    assert_true(failure.is_failure(),
          "RetentionOutcome should keep retention failures representable with existing contracts result codes only");
    assert_true(!invalid_cleanup.has_consistent_state(),
          "RetentionOutcome should reject delete results that do not carry cleanup_evidence and an archive-linked trace");
  }

}  // namespace

int main() {
  try {
    test_audit_link_adapter_freezes_placeholder_linking_interface();
    test_audit_logger_interface_freezes_write_and_export_signatures();
    test_audit_logger_interface_rejects_invalid_event_or_context_on_write_path();
    test_audit_health_probe_interface_freezes_evaluate_signature();
    test_audit_health_status_accepts_degraded_and_unavailable_snapshots();
    test_audit_health_status_rejects_invalid_ready_and_reason_combinations();
    test_audit_retention_interface_freezes_apply_signature_and_outcome_shape();
    test_retention_outcome_accepts_failure_and_rejects_cleanup_without_trace();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
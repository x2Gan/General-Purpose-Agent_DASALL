#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "LogEvent.h"
#include "ProfileTelemetryAdapter.h"
#include "audit/AuditTypes.h"
#include "audit/IAuditLogger.h"
#include "logging/ILogger.h"
#include "support/TestAssertions.h"

namespace {

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    if (event.module.empty() || !event.attrs_are_serializable()) {
      return dasall::infra::logging::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "log event must keep module and attrs valid",
          "profiles.telemetry.log",
          "RecordingLogger");
    }

    records.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline& deadline) override {
    if (!deadline.is_valid()) {
      return dasall::infra::logging::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "flush deadline must be positive",
          "profiles.telemetry.flush",
          "RecordingLogger");
    }

    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    level_ = level;
  }

  std::vector<dasall::infra::LogEvent> records;

 private:
  dasall::infra::logging::LogLevel level_ = dasall::infra::logging::LogLevel::Info;
};

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
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

    records.push_back(event);
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
        .records = records,
        .next_page_token = std::string(),
        .truncated = false,
        .checksum = std::string("profile-telemetry-export"),
    };
  }

  std::vector<dasall::infra::AuditEvent> records;
};

void test_profile_telemetry_adapter_records_activation_success() {
  using dasall::infra::AuditOutcome;
  using dasall::profiles::ProfileTelemetryAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecordingLogger logger;
  RecordingAuditLogger audit_logger;
  ProfileTelemetryAdapter adapter(logger, audit_logger);

  const auto result = adapter.record_activation_success(
      "desktop_full", "desktop_full", "baseline_activate", "profiles.runtime");

  assert_true(result.ok(), "activation success should write both log and audit events");
  assert_true(result.references_only_contract_error_types(),
              "successful dispatch should stay within frozen contracts result categories");
  assert_equal(1, static_cast<int>(logger.records.size()),
               "activation success should emit one log event");
  assert_equal(1, static_cast<int>(audit_logger.records.size()),
               "activation success should emit one audit event");
  assert_true(logger.records.front().message == "runtime_policy_activated",
              "activation success should use the frozen log action name");
  assert_true(logger.records.front().attrs.at("activation_mode") == "baseline_activate",
              "activation success should surface activation_mode in log attrs");
  assert_true(audit_logger.records.front().outcome == AuditOutcome::Succeeded,
              "activation success should map to audit success outcome");
}

void test_profile_telemetry_adapter_records_rejected_and_fallback_events() {
  using dasall::infra::AuditOutcome;
  using dasall::profiles::ProfileTelemetryAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecordingLogger logger;
  RecordingAuditLogger audit_logger;
  ProfileTelemetryAdapter adapter(logger, audit_logger);

  const auto rejected = adapter.record_reload_rejected(
      "edge_balanced", "module-mismatch", "profiles.runtime");
  const auto fallback = adapter.record_fallback_lkg(
      "edge_balanced", "edge_balanced", "schema-invalid", "profiles.runtime");

  assert_true(rejected.ok(), "reload rejection should write both observability channels");
  assert_true(fallback.ok(), "fallback_lkg should write both observability channels");
  assert_equal(2, static_cast<int>(logger.records.size()),
               "rejected and fallback flows should each emit one log event");
  assert_equal(2, static_cast<int>(audit_logger.records.size()),
               "rejected and fallback flows should each emit one audit event");
  assert_true(logger.records.front().message == "runtime_policy_reload_rejected",
              "reload rejection should use the frozen rejected action name");
  assert_true(logger.records.back().message == "runtime_policy_fallback_lkg",
              "fallback flow should use the frozen fallback action name");
  assert_true(logger.records.front().attrs.at("reason_code") == "module-mismatch",
              "reload rejection should include reason_code in log attrs");
  assert_true(audit_logger.records.front().outcome == AuditOutcome::Rejected,
              "reload rejection should map to audit rejected outcome");
  assert_true(audit_logger.records.back().outcome == AuditOutcome::Escalated,
              "fallback_lkg should map to escalated audit outcome");
}

void test_profile_telemetry_adapter_rejects_incomplete_payloads_without_emitting_side_effects() {
  using dasall::profiles::ProfileTelemetryAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecordingLogger logger;
  RecordingAuditLogger audit_logger;
  ProfileTelemetryAdapter adapter(logger, audit_logger);

  const auto result = adapter.record_activation_success(
      "", "desktop_full", "baseline_activate", "profiles.runtime");

  assert_true(!result.ok(), "missing requested_profile_id should be rejected explicitly");
  assert_true(result.references_only_contract_error_types(),
              "invalid payload rejection should stay within contracts error types");
  assert_equal(0, static_cast<int>(logger.records.size()),
               "invalid payload should not emit a partial log side effect");
  assert_equal(0, static_cast<int>(audit_logger.records.size()),
               "invalid payload should not emit a partial audit side effect");
}

}  // namespace

int main() {
  try {
    test_profile_telemetry_adapter_records_activation_success();
    test_profile_telemetry_adapter_records_rejected_and_fallback_events();
    test_profile_telemetry_adapter_rejects_incomplete_payloads_without_emitting_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
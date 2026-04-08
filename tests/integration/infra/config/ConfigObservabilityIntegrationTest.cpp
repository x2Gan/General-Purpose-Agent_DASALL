#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "config/ConfigCenterFacade.h"
#include "logging/ILogger.h"
#include "audit/IAuditLogger.h"
#include "support/TestAssertions.h"

namespace {

struct ConfigObservabilityDispatchResult {
  bool ok = false;
  bool partial_success = false;
  bool log_written = false;
  bool audit_written = false;
  bool payload_valid = false;
};

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    if (event.module.empty() || event.message.empty() || !event.attrs_are_serializable()) {
      return dasall::infra::logging::LogWriteResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "log event must keep module, message, and attrs valid",
          "config.observability.log",
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
          "config.observability.flush",
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
        .checksum = std::string("config-observability-export"),
    };
  }

  std::vector<dasall::infra::AuditEvent> records;
};

class FailingAuditLogger final : public dasall::infra::audit::IAuditLogger {
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
        .accepted = false,
        .persisted = false,
        .fallback_used = false,
        .error_code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery& query) override {
    static_cast<void>(query);
    return dasall::infra::ExportResult{};
  }
};

dasall::infra::config::ConfigStartupContext make_startup_context() {
  return dasall::infra::config::ConfigStartupContext{
      .requested_profile_id = std::string("desktop_full"),
      .deployment_source_ref = std::string("deploy://site-001/config.yaml"),
      .runtime_overlay_source_ref = std::string("ops://window/bootstrap"),
      .actor_ref = std::string("runtime-bootstrap"),
      .load_runtime_overlay = true,
  };
}

dasall::infra::config::ConfigPatch make_runtime_override_patch() {
  return dasall::infra::config::ConfigPatch{
      .patch_id = std::string("runtime-patch-016"),
      .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/302"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 1,
      .reason_code = std::string("temporary_debug"),
      .expires_at = std::string("2026-04-02T03:00:00Z"),
      .patches = {dasall::infra::config::ConfigPatchEntry{
          .op = dasall::infra::config::ConfigPatchOperation::Replace,
          .key_path = std::string("infra.config.validation.strict"),
          .value = dasall::infra::config::TypedConfig{
              .key_path = std::string("infra.config.validation.strict"),
              .value_type = dasall::infra::config::ConfigValueType::Boolean,
              .serialized_value = std::string("false"),
              .schema_version = std::string("1"),
              .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/302"),
              .secret_backed = false,
          },
      }},
  };
}

ConfigObservabilityDispatchResult record_config_change(
    const dasall::infra::config::ConfigDiff& diff,
    const std::string& actor,
    const std::string& evidence_ref,
    dasall::infra::logging::ILogger& logger,
    dasall::infra::audit::IAuditLogger& audit_logger) {
  ConfigObservabilityDispatchResult result{};
  result.payload_valid = diff.is_valid() && !actor.empty() && !evidence_ref.empty();
  if (!result.payload_valid) {
    return result;
  }

  const auto& first_change = diff.changes.front();
  dasall::infra::logging::LogEvent log_event{
      .level = dasall::infra::LogLevel::Info,
      .module = std::string("infra.config"),
      .message = std::string("config.apply_override"),
      .attrs = {
          {"key_path", first_change.key_path},
          {"from_version", std::to_string(diff.from_version)},
          {"to_version", std::to_string(diff.to_version)},
          {"outcome", std::string("succeeded")},
          {"evidence_ref", evidence_ref},
      },
      .ts = 1712016002000,
  };

  const auto log_result = logger.log(log_event);
  result.log_written = log_result.ok;

  dasall::infra::AuditEvent audit_event{
      .event_id = std::string("config-audit://diff/") + std::to_string(diff.to_version),
      .action = std::string("config.apply_override"),
      .actor = actor,
      .target = first_change.key_path,
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {
          .kind = dasall::infra::AuditEvidenceKind::ToolResult,
          .ref = evidence_ref,
      },
      .side_effects = {
          std::string("from_version=") + std::to_string(diff.from_version),
          std::string("to_version=") + std::to_string(diff.to_version),
      },
      .timestamp = 1712016002000,
  };

  const auto audit_result = audit_logger.write_audit(audit_event, dasall::infra::AuditContext{});
  result.audit_written = audit_result.is_success() || audit_result.is_degraded_success();
  result.ok = result.log_written && result.audit_written;
  result.partial_success = result.log_written != result.audit_written;
  return result;
}

void test_config_observability_integration_records_log_and_audit_for_runtime_patch() {
  using dasall::infra::config::ConfigCenterFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigCenterFacade facade;
  std::optional<dasall::infra::config::ConfigDiff> observed_diff;

  const auto subscription = facade.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config.validation."),
      .subscriber_id = std::string("config-observability-subscriber"),
      .callback = [&](const dasall::infra::config::ConfigDiff& diff) { observed_diff = diff; },
  });
  assert_true(subscription.has_value() && subscription->active,
              "ConfigCenterFacade should accept the observability integration subscriber");

  const auto load_result = facade.load_layers(make_startup_context());
  assert_true(load_result.applied,
              "ConfigCenterFacade should bootstrap successfully before observability integration runs");

  const auto apply_result = facade.apply_override(make_runtime_override_patch());
  assert_true(apply_result.applied,
              "ConfigCenterFacade should apply the runtime override before observability sinks are evaluated");
  assert_true(observed_diff.has_value() && observed_diff->is_valid(),
              "ConfigCenterFacade should publish a valid diff for the observability bridge to consume");

  RecordingLogger logger;
  RecordingAuditLogger audit_logger;
  const auto dispatch_result = record_config_change(
      *observed_diff, std::string("ops-user"), std::string("tool-call-302"), logger, audit_logger);

  assert_true(dispatch_result.ok,
              "config observability bridge should report success when both logger and audit sinks accept the diff");
  assert_true(!dispatch_result.partial_success,
              "config observability bridge should not report partial success when both sinks persist the event");
  assert_equal(1, static_cast<int>(logger.records.size()),
               "runtime patch observability should emit one structured log record");
  assert_equal(1, static_cast<int>(audit_logger.records.size()),
               "runtime patch observability should emit one audit record");
  assert_true(logger.records.front().module == "infra.config" &&
                  logger.records.front().message == "config.apply_override",
              "config observability log record should keep the frozen module and action name");
  assert_true(logger.records.front().attrs.at("key_path") == "infra.config.validation.strict" &&
                  logger.records.front().attrs.at("to_version") == "2",
              "config observability log attrs should retain key_path and version details");
  assert_true(audit_logger.records.front().action == "config.apply_override" &&
                  audit_logger.records.front().target == "infra.config.validation.strict" &&
                  audit_logger.records.front().outcome == dasall::infra::AuditOutcome::Succeeded,
              "config observability audit record should preserve action, target, and success outcome");
}

void test_config_observability_integration_rejects_invalid_payload_without_emitting_side_effects() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  RecordingLogger logger;
  RecordingAuditLogger audit_logger;
  const auto dispatch_result = record_config_change(dasall::infra::config::ConfigDiff{},
                                                    std::string(),
                                                    std::string(),
                                                    logger,
                                                    audit_logger);

  assert_true(!dispatch_result.ok && !dispatch_result.payload_valid,
              "config observability bridge should reject invalid payloads explicitly");
  assert_equal(0, static_cast<int>(logger.records.size()),
               "invalid config observability payload should not emit partial log side effects");
  assert_equal(0, static_cast<int>(audit_logger.records.size()),
               "invalid config observability payload should not emit partial audit side effects");
}

void test_config_observability_integration_reports_partial_failure_for_audit_sink_errors() {
  using dasall::infra::config::ConfigCenterFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigCenterFacade facade;
  std::optional<dasall::infra::config::ConfigDiff> observed_diff;
  const auto subscription = facade.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config.validation."),
      .subscriber_id = std::string("config-observability-failure-subscriber"),
      .callback = [&](const dasall::infra::config::ConfigDiff& diff) { observed_diff = diff; },
  });
  assert_true(subscription.has_value() && subscription->active,
              "ConfigCenterFacade should accept the subscriber used for partial failure validation");

  assert_true(facade.load_layers(make_startup_context()).applied,
              "ConfigCenterFacade should bootstrap before partial failure validation");
  assert_true(facade.apply_override(make_runtime_override_patch()).applied,
              "ConfigCenterFacade should still produce a diff for partial failure validation");
  assert_true(observed_diff.has_value() && observed_diff->is_valid(),
              "observability partial failure validation still requires a valid diff");

  RecordingLogger logger;
  FailingAuditLogger audit_logger;
  const auto dispatch_result = record_config_change(
      *observed_diff, std::string("ops-user"), std::string("tool-call-303"), logger, audit_logger);

  assert_true(!dispatch_result.ok && dispatch_result.partial_success,
              "config observability bridge should surface logger-success/audit-failure as a partial success state");
  assert_true(dispatch_result.log_written && !dispatch_result.audit_written,
              "config observability bridge should report which sink succeeded and which sink failed");
  assert_equal(1, static_cast<int>(logger.records.size()),
               "audit sink failure should not suppress the already emitted structured log record");
}

}  // namespace

int main() {
  try {
    test_config_observability_integration_records_log_and_audit_for_runtime_patch();
    test_config_observability_integration_rejects_invalid_payload_without_emitting_side_effects();
    test_config_observability_integration_reports_partial_failure_for_audit_sink_errors();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
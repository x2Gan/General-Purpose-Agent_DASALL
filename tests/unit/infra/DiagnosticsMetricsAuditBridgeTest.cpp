#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audit/IAuditLogger.h"
#include "diagnostics/DiagnosticsAuditBridge.h"
#include "diagnostics/DiagnosticsMetricsBridge.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class ScriptedMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples.push_back(sample);
    if (!scripted_results.empty()) {
      const auto result = scripted_results.front();
      scripted_results.pop_front();
      return result;
    }

    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://diagnostics/record");
  }

  std::deque<dasall::infra::metrics::MetricsOperationStatus> scripted_results;
  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class ScriptedProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit ScriptedProvider(std::shared_ptr<ScriptedMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://diagnostics/provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://diagnostics/provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://diagnostics/provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<ScriptedMeter> meter_;
};

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

[[nodiscard]] bool has_identity(std::vector<dasall::infra::metrics::MetricIdentity> identities,
                                std::string_view name,
                                dasall::infra::metrics::MetricType type,
                                std::string_view unit) {
  return std::any_of(identities.begin(), identities.end(), [&](const auto& identity) {
    return identity.name == name && identity.type == type && identity.unit == unit;
  });
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

void test_diagnostics_metrics_bridge_emits_frozen_metric_families_with_scope_and_labels() {
  using dasall::infra::diagnostics::DiagnosticsMetricKind;
  using dasall::infra::diagnostics::DiagnosticsMetricSignal;
  using dasall::infra::diagnostics::DiagnosticsMetricsBridge;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<ScriptedMeter>();
  auto provider = std::make_shared<ScriptedProvider>(meter);
  DiagnosticsMetricsBridge bridge(provider, "edge_balanced");

  const auto command_result = bridge.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = 1712577600000,
      .stage = std::string("execute.health_snapshot"),
      .outcome = std::string("success"),
      .error_code = std::string("none"),
  });
  const auto export_result = bridge.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::ExportTotal,
      .value = 1.0,
      .ts_unix_ms = 1712577600001,
      .stage = std::string("export.remote_upload"),
      .outcome = std::string("rejected"),
      .error_code = std::string("INF_E_DIAG_REMOTE_EXPORT_DISABLED"),
  });

  assert_true(command_result.emitted && export_result.emitted,
              "DiagnosticsMetricsBridge should emit accepted diagnostics metric samples");
  assert_equal(std::string("infra.diagnostics"),
               provider->last_scope.name,
               "DiagnosticsMetricsBridge should request the frozen infra.diagnostics meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope.version,
               "DiagnosticsMetricsBridge should preserve the frozen meter scope version");
  assert_equal(7, static_cast<int>(meter->created_identities.size()),
               "DiagnosticsMetricsBridge should register exactly the seven frozen diagnostics metric families on first emit");
  assert_true(has_identity(meter->created_identities,
                           "infra_diag_command_total",
                           MetricType::Counter,
                           "1") &&
                  has_identity(meter->created_identities,
                               "infra_diag_exec_latency_ms",
                               MetricType::Histogram,
                               "ms") &&
                  has_identity(meter->created_identities,
                               "infra_diag_export_total",
                               MetricType::Counter,
                               "1"),
              "DiagnosticsMetricsBridge should preserve the frozen name/type/unit contract for command, latency and export metrics");
  assert_equal(2, static_cast<int>(meter->recorded_samples.size()),
               "DiagnosticsMetricsBridge should record one sample per accepted emit call");
  assert_true(meter->recorded_samples.front().labels.module == "diagnostics" &&
                  meter->recorded_samples.front().labels.stage ==
                      "execute.health_snapshot" &&
                  meter->recorded_samples.front().labels.profile == "edge_balanced" &&
                  meter->recorded_samples.front().labels.outcome == "success" &&
                  meter->recorded_samples.front().labels.error_code == "none",
              "DiagnosticsMetricsBridge should project command success into the frozen diagnostics metric label tuple");
  assert_true(meter->recorded_samples.back().labels.stage == "export.remote_upload" &&
                  meter->recorded_samples.back().labels.outcome == "rejected" &&
                  meter->recorded_samples.back().labels.error_code ==
                      "INF_E_DIAG_REMOTE_EXPORT_DISABLED",
              "DiagnosticsMetricsBridge should project remote export denial into the frozen stage/outcome/error_code tuple");
}

void test_diagnostics_metrics_bridge_rejects_non_whitelist_stage_and_error_code() {
  using dasall::infra::diagnostics::DiagnosticsMetricKind;
  using dasall::infra::diagnostics::DiagnosticsMetricSignal;
  using dasall::infra::diagnostics::DiagnosticsMetricsBridge;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<ScriptedMeter>();
  auto provider = std::make_shared<ScriptedProvider>(meter);
  DiagnosticsMetricsBridge bridge(provider);

  const auto result = bridge.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::CommandTotal,
      .value = 1.0,
      .ts_unix_ms = 1712577600100,
      .stage = std::string("execute.secret_dump"),
      .outcome = std::string("success"),
      .error_code = std::string("none"),
  });

  assert_true(!result.emitted && result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "DiagnosticsMetricsBridge should reject non-whitelist stage labels before sample emission");
  assert_true(meter->recorded_samples.empty(),
              "DiagnosticsMetricsBridge should not emit any sample once the local signal guard fails");
}

void test_diagnostics_metrics_bridge_surfaces_provider_failures_without_recursive_blocking() {
  using dasall::infra::diagnostics::DiagnosticsMetricKind;
  using dasall::infra::diagnostics::DiagnosticsMetricSignal;
  using dasall::infra::diagnostics::DiagnosticsMetricsBridge;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::tests::support::assert_true;

  DiagnosticsMetricsBridge bridge(nullptr, "desktop_full");

  const auto result = bridge.emit(DiagnosticsMetricSignal{
      .kind = DiagnosticsMetricKind::SafeModeEnterTotal,
      .value = 1.0,
      .ts_unix_ms = 1712577600200,
      .stage = std::string("safe_mode"),
      .outcome = std::string("degraded"),
      .error_code = std::string("none"),
  });

  assert_true(!result.emitted && result.bridge_degraded && result.has_consistent_state(),
              "DiagnosticsMetricsBridge should keep provider-not-ready visible as a local degraded bridge result");
  assert_true(result.metrics_error_code == MetricsErrorCode::ProviderNotReady &&
                  bridge.is_degraded() && !bridge.has_active_meter(),
              "DiagnosticsMetricsBridge should retain provider-not-ready in bridge-local status instead of fabricating a successful emit");
}

  void test_diagnostics_audit_bridge_emits_rejected_remote_export_payload() {
    using dasall::contracts::ResultCode;
    using dasall::infra::AuditEvidenceKind;
    using dasall::infra::AuditOutcome;
    using dasall::infra::diagnostics::DiagnosticsAuditBridge;
    using dasall::infra::diagnostics::DiagnosticsSnapshot;
    using dasall::infra::diagnostics::DiagnosticsCommand;
    using dasall::infra::diagnostics::ExportFormat;
    using dasall::infra::diagnostics::ExportTarget;
    using dasall::infra::diagnostics::RedactionProfile;
    using dasall::infra::diagnostics::SnapshotExportRequest;
    using dasall::infra::diagnostics::SnapshotExportResult;
    using dasall::tests::support::assert_equal;
    using dasall::tests::support::assert_true;

    auto logger = std::make_shared<ScriptedAuditLogger>();
    DiagnosticsAuditBridge bridge(logger);
    const auto result = bridge.write_remote_export_event(
      DiagnosticsSnapshot{
        .snapshot_id = std::string("diag-snapshot-022"),
        .command = DiagnosticsCommand{
          .command_id = std::string("diag-command-022"),
          .command_name = std::string("health.snapshot"),
          .args = {},
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string("actor://redacted"),
        },
        .collected_at = std::string("2026-04-07T13:00:00Z"),
        .summary = std::string("diagnostics redacted health snapshot"),
        .evidence_refs = {std::string("health://snapshot/diag-snapshot-022")},
        .redaction_profile = RedactionProfile::Strict,
        .exporter_hint = std::string("local_file"),
      },
      SnapshotExportRequest{
        .snapshot_id = std::string("diag-snapshot-022"),
        .target = ExportTarget::RemoteUpload,
        .format = ExportFormat::Json,
        .target_ref = std::string("https://diagnostics.example.test/upload"),
      },
      SnapshotExportResult::failure(ResultCode::PolicyDenied,
                    std::string("INF_E_DIAG_REMOTE_EXPORT_DISABLED: remote export disabled"),
                    std::string("diagnostics.export_snapshot"),
                    std::string("https://diagnostics.example.test/upload")));
    const auto status = bridge.get_status();

    assert_true(result.emitted && result.is_valid(),
          "DiagnosticsAuditBridge should emit a required-sink audit payload for rejected remote export requests");
    assert_true(status.is_valid() && status.emitted_total == 1 && !status.degraded,
          "DiagnosticsAuditBridge should remain healthy after persisting the rejected remote export audit event");

    const auto& event = logger->events.back();
    const auto& context = logger->contexts.back();
    assert_equal(std::string("diagnostics.remote_export"),
           event.action,
           "DiagnosticsAuditBridge should map remote export requests to the frozen diagnostics.remote_export action");
    assert_equal(std::string("diagnostics.export:https://diagnostics.example.test/upload"),
           event.target,
           "DiagnosticsAuditBridge should keep remote export audit targets inside the frozen diagnostics.export namespace");
    assert_true(event.outcome == AuditOutcome::Rejected,
          "DiagnosticsAuditBridge should map remote export gate rejections to AuditOutcome::Rejected");
    assert_true(event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
            event.evidence_ref.ref == "snapshot://diag-snapshot-022",
          "DiagnosticsAuditBridge should reference retained snapshots through ToolResult evidence refs");
    assert_true(event.actor == "actor://redacted" &&
            has_side_effect(event, "target_ref:https://diagnostics.example.test/upload") &&
            has_side_effect(event, "format:json") &&
            has_side_effect(event, "result_code:PolicyDenied") &&
            has_side_effect(event, "request_scope:runtime"),
          "DiagnosticsAuditBridge should keep actors redacted and serialize only the frozen stable side effects");
    assert_equal(std::string("infra.diagnostics"),
           context.worker_type,
           "DiagnosticsAuditBridge should pin worker_type=infra.diagnostics on emitted audit contexts");
  }

  void test_diagnostics_audit_bridge_blocks_when_required_sink_is_missing() {
    using dasall::contracts::ResultCode;
    using dasall::infra::diagnostics::DiagnosticsAuditBridge;
    using dasall::infra::diagnostics::DiagnosticsSnapshot;
    using dasall::infra::diagnostics::DiagnosticsCommand;
    using dasall::infra::diagnostics::ExportFormat;
    using dasall::infra::diagnostics::ExportTarget;
    using dasall::infra::diagnostics::RedactionProfile;
    using dasall::infra::diagnostics::SnapshotExportRequest;
    using dasall::infra::diagnostics::SnapshotExportResult;
    using dasall::tests::support::assert_true;

    DiagnosticsAuditBridge bridge;
    const auto result = bridge.write_remote_export_event(
      DiagnosticsSnapshot{
        .snapshot_id = std::string("diag-snapshot-023"),
        .command = DiagnosticsCommand{
          .command_id = std::string("diag-command-023"),
          .command_name = std::string("queue.stats"),
          .args = {std::string("--queue=redacted")},
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string("actor://redacted"),
        },
        .collected_at = std::string("2026-04-07T13:00:00Z"),
        .summary = std::string("diagnostics redacted queue stats"),
        .evidence_refs = {std::string("metrics://queue/diag-snapshot-023")},
        .redaction_profile = RedactionProfile::Strict,
        .exporter_hint = std::string("local_file"),
      },
      SnapshotExportRequest{
        .snapshot_id = std::string("diag-snapshot-023"),
        .target = ExportTarget::RemoteUpload,
        .format = ExportFormat::Json,
        .target_ref = std::string("https://diagnostics.example.test/upload"),
      },
      SnapshotExportResult::failure(ResultCode::PolicyDenied,
                    std::string("INF_E_DIAG_REMOTE_EXPORT_DISABLED: remote export disabled"),
                    std::string("diagnostics.export_snapshot"),
                    std::string("https://diagnostics.example.test/upload")));
    const auto status = bridge.get_status();

    assert_true(!result.emitted && result.is_valid(),
          "DiagnosticsAuditBridge should surface missing audit sinks as explicit failures instead of silently allowing remote export");
    assert_true(result.result_code == ResultCode::RuntimeRetryExhausted &&
            result.references_only_contract_error_types(),
          "DiagnosticsAuditBridge should normalize missing required sinks to RuntimeRetryExhausted within contracts error semantics");
    assert_true(status.is_valid() && status.degraded && status.emit_failures == 1 &&
            status.last_error_code == ResultCode::RuntimeRetryExhausted,
          "DiagnosticsAuditBridge should retain the last missing-sink failure for follow-up health and retry decisions");
  }

  void test_diagnostics_audit_bridge_maps_command_extension_to_redacted_command_namespace() {
    using dasall::contracts::ResultCode;
    using dasall::infra::AuditEvidenceKind;
    using dasall::infra::AuditOutcome;
    using dasall::infra::diagnostics::DiagnosticsAuditBridge;
    using dasall::infra::diagnostics::DiagnosticsAuditEventOutcome;
    using dasall::infra::diagnostics::DiagnosticsCommand;
    using dasall::tests::support::assert_equal;
    using dasall::tests::support::assert_true;

    auto logger = std::make_shared<ScriptedAuditLogger>();
    DiagnosticsAuditBridge bridge(logger);
    const auto result = bridge.write_command_extension_event(
      DiagnosticsCommand{
        .command_id = std::string("diag-extension-001"),
        .command_name = std::string("vendor.secret.inspect"),
        .args = {std::string("--scope=minimal")},
        .request_scope = std::string("runtime"),
        .timeout_ms = 3000,
        .actor_ref = std::string("ops-user"),
      },
      DiagnosticsAuditEventOutcome::Rejected,
      ResultCode::PolicyDenied,
      std::string("command://diag-extension-001/denied"));

    assert_true(result.emitted && result.is_valid(),
          "DiagnosticsAuditBridge should support the reserved command-extension audit contract without widening the executor surface");

    const auto& event = logger->events.back();
    assert_equal(std::string("diagnostics.command_extension"),
           event.action,
           "DiagnosticsAuditBridge should map reserved extension events to the frozen diagnostics.command_extension action");
    assert_equal(std::string("diagnostics.command:vendor.secret.inspect"),
           event.target,
           "DiagnosticsAuditBridge should keep extension audit targets inside the diagnostics.command namespace");
    assert_true(event.outcome == AuditOutcome::Rejected &&
            event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
            event.evidence_ref.ref == "command://diag-extension-001" &&
            event.actor == "actor://redacted",
          "DiagnosticsAuditBridge should keep extension evidence inside ToolResult refs and normalize non-controlled actors to actor://redacted");
    assert_true(has_side_effect(event, "result_code:PolicyDenied") &&
            has_side_effect(event, "detail_ref:command://diag-extension-001/denied") &&
            has_side_effect(event, "request_scope:runtime"),
          "DiagnosticsAuditBridge should serialize only the frozen result/detail/request_scope facts for extension events");
  }

}  // namespace

int main() {
  try {
    test_diagnostics_metrics_bridge_emits_frozen_metric_families_with_scope_and_labels();
    test_diagnostics_metrics_bridge_rejects_non_whitelist_stage_and_error_code();
    test_diagnostics_metrics_bridge_surfaces_provider_failures_without_recursive_blocking();
    test_diagnostics_audit_bridge_emits_rejected_remote_export_payload();
    test_diagnostics_audit_bridge_blocks_when_required_sink_is_missing();
    test_diagnostics_audit_bridge_maps_command_extension_to_redacted_command_namespace();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
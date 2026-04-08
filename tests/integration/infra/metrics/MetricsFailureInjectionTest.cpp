#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "InfraContext.h"
#include "audit/IAuditLogger.h"
#include "logging/ILogger.h"
#include "metrics/MetricReaderScheduler.h"
#include "metrics/MetricsAuditBridge.h"
#include "metrics/MetricsConfigPolicy.h"
#include "metrics/MetricsExporterAdapter.h"
#include "metrics/MetricsFacade.h"
#include "metrics/MetricsLoggingBridge.h"
#include "metrics/MetricsRecovery.h"
#include "support/TestAssertions.h"

namespace {

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    events.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    last_level = level;
  }

  std::vector<dasall::infra::logging::LogEvent> events;
  dasall::infra::logging::LogLevel last_level =
      dasall::infra::logging::LogLevel::Info;
};

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
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

  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] dasall::infra::metrics::MeterScope make_scope() {
  return dasall::infra::metrics::MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::metrics::MetricsProviderConfig make_provider_config() {
  return dasall::infra::metrics::MetricsProviderConfig{
      .enabled = true,
      .provider_type = std::string("internal"),
      .exporter_type = std::string("prom_text"),
      .reader_interval_ms = 1000,
      .exporter_timeout_ms = 1,
  };
}

[[nodiscard]] dasall::infra::metrics::MetricsResolvedConfig make_timeouting_runtime_config() {
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.reader_interval_ms = 1000U;
  profile_patch.exporter_timeout_ms = 1U;
  return policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{});
}

[[nodiscard]] dasall::infra::InfraContext make_infra_context() {
  return dasall::infra::InfraContext{
      .request_id = std::string("req-metrics-integration-023"),
      .session_id = std::string("session-metrics-integration-023"),
      .trace_id = std::string("trace-metrics-integration-023"),
      .task_id = std::string("task-metrics-integration-023"),
      .parent_task_id = std::string("parent-metrics-integration-023"),
      .lease_id = std::string("lease-metrics-integration-023"),
  };
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::any_of(event.side_effects.begin(),
                     event.side_effects.end(),
                     [&](const std::string& side_effect) {
                       return side_effect == expected;
                     });
}

[[nodiscard]] dasall::infra::metrics::AggregationSnapshot make_snapshot() {
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricLabels;
  using dasall::infra::metrics::MetricSample;
  using dasall::infra::metrics::MetricsFacade;
  using dasall::infra::metrics::MetricType;
  using dasall::tests::support::assert_true;

  MetricsFacade facade;
  assert_true(facade.init(make_provider_config()).ok,
              "metrics failure injection should initialize MetricsFacade before building a failure snapshot");

  const auto meter = facade.get_meter(make_scope());
  assert_true(static_cast<bool>(meter),
              "metrics failure injection should resolve the frozen infra.metrics meter scope");

  const MetricIdentity counter_identity{
      .name = std::string("metrics_export_total"),
      .type = MetricType::Counter,
      .unit = std::string("1"),
      .description = std::string("successful metrics exports"),
  };
  const MetricIdentity latency_identity{
      .name = std::string("metrics_export_latency_ms"),
      .type = MetricType::Histogram,
      .unit = std::string("ms"),
      .description = std::string("metrics export latency"),
  };

  assert_true(meter->create_counter(counter_identity).has_value() &&
                  meter->create_histogram(latency_identity).has_value(),
              "metrics failure injection should create the exporter counter and latency histogram before recording the failure batch");

  const auto counter_result = meter->record(MetricSample{
      .identity_ref = counter_identity,
      .value = 1.0,
      .ts_unix_ms = 1712566800000,
      .labels = MetricLabels{
          .module = std::string("metrics"),
          .stage = std::string("export"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("success"),
          .error_code = std::string(),
      },
  });
  const auto latency_result = meter->record(MetricSample{
      .identity_ref = latency_identity,
      .value = 4.0,
      .ts_unix_ms = 1712566800001,
      .labels = MetricLabels{
          .module = std::string("metrics"),
          .stage = std::string("export"),
          .profile = std::string("desktop_full"),
          .outcome = std::string("success"),
          .error_code = std::string(),
      },
  });

  assert_true(counter_result.ok && latency_result.ok,
              "metrics failure injection should record a valid export snapshot before simulating exporter timeouts");
  return facade.aggregation_snapshot();
}

void test_metrics_failure_injection_enters_degraded_mode_and_recovers() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::metrics::MetricExportBatch;
  using dasall::infra::metrics::MetricReaderScheduler;
  using dasall::infra::metrics::MetricsAuditBridge;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsExporterAdapter;
  using dasall::infra::metrics::MetricsLoggingBridge;
  using dasall::infra::metrics::MetricsRecovery;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto snapshot = make_snapshot();
  MetricReaderScheduler scheduler(make_timeouting_runtime_config());
  MetricsExporterAdapter adapter(make_timeouting_runtime_config());
  auto logger = std::make_shared<RecordingLogger>();
  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  auto logging_bridge = std::make_shared<MetricsLoggingBridge>(logger);
  MetricsAuditBridge audit_bridge(audit_logger);
  MetricsRecovery recovery(2U, logging_bridge);

  const auto first_tick = scheduler.schedule_tick(1000, snapshot);
  const auto first_batch = scheduler.pop_next_batch();
  const auto first_failure = adapter.export_batch(*first_batch);
  const auto first_observation = recovery.observe_export_result(first_failure,
                                                               adapter.module_snapshot());

  assert_true(first_tick.triggered && first_batch.has_value() && !first_failure.ok &&
                  first_observation.ok && !recovery.is_degraded() &&
                  recovery.consecutive_failure_total() == 1U && logger->events.empty(),
              "metrics failure injection should keep the first exporter timeout observable without entering degraded mode before the threshold is crossed");

  const auto second_tick = scheduler.schedule_tick(2000, snapshot);
  const auto second_batch = scheduler.pop_next_batch();
  const auto second_failure = adapter.export_batch(*second_batch);
  const auto second_observation = recovery.observe_export_result(second_failure,
                                                                adapter.module_snapshot());
  const auto degraded_audit_result = audit_bridge.write_recovery_event(
      *recovery.last_event(),
      make_infra_context());

  assert_true(second_tick.triggered && second_batch.has_value() && !second_failure.ok &&
                  second_observation.ok && recovery.is_degraded() &&
                  recovery.last_error_code() == MetricsErrorCode::ExportTimeout &&
                  recovery.degrade_enter_total() == 1U && logger->events.size() == 1U &&
                  degraded_audit_result.emitted && audit_logger->events.size() == 1U,
              "metrics failure injection should enter degraded mode after the second exporter timeout and emit both logging and audit recovery evidence");
  assert_true(logger->events.back().module == "metrics" &&
                  logger->events.back().attrs.at("action") == "enter_degraded" &&
                  logger->events.back().attrs.at("degraded") == "true" &&
                  logger->events.back().attrs.at("error_code") == "MET_E_EXPORT_TIMEOUT",
              "metrics failure injection should serialize the degraded recovery transition through MetricsLoggingBridge");
  assert_true(audit_logger->events.back().action == "metrics.enter_degraded" &&
                  audit_logger->events.back().target == "metrics:recovery" &&
                  audit_logger->events.back().outcome == AuditOutcome::Escalated &&
                  has_side_effect(audit_logger->events.back(), "error_code:MET_E_EXPORT_TIMEOUT") &&
                  audit_logger->contexts.back().worker_type == "infra.metrics",
              "metrics failure injection should keep degraded recovery evidence inside the frozen metrics audit namespace");

  const auto recovery_export = adapter.export_batch(MetricExportBatch{
      .batch_id = std::string("metrics-batch://recovery/1"),
      .sample_count = 1U,
      .exporter_type = std::string("noop"),
  });
  const auto recovery_observation = recovery.observe_export_result(recovery_export,
                                                                  adapter.module_snapshot());
  const auto healthy_audit_result = audit_bridge.write_recovery_event(
      *recovery.last_event(),
      make_infra_context());

  assert_true(recovery_export.ok && recovery_observation.ok && !recovery.is_degraded() &&
                  recovery.recovery_success_total() == 1U && logger->events.size() == 2U &&
                  healthy_audit_result.emitted && audit_logger->events.size() == 2U,
              "metrics failure injection should recover from degraded mode once the exporter returns to a healthy noop success path");
  assert_true(logger->events.back().attrs.at("action") == "recover_to_healthy" &&
                  logger->events.back().attrs.at("degraded") == "false",
              "metrics failure injection should emit a structured recover_to_healthy log event after the exporter recovers");
  assert_true(audit_logger->events.back().action == "metrics.recover_to_healthy" &&
                  audit_logger->events.back().outcome == AuditOutcome::Succeeded &&
                  has_side_effect(audit_logger->events.back(), "error_code:none"),
              "metrics failure injection should emit a success-shaped audit event when recovery exits degraded mode");
  assert_equal(2,
               static_cast<int>(logger->events.size()),
               "metrics failure injection should emit exactly one degraded event and one recovery event through the logging bridge");
}

}  // namespace

int main() {
  try {
    test_metrics_failure_injection_enters_degraded_mode_and_recovers();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
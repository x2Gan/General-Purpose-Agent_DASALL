#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "audit/IAuditLogger.h"
#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "watchdog/DeadlineWheel.h"
#include "watchdog/HeartbeatIngestor.h"
#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/RecoveryRequestEmitter.h"
#include "watchdog/TimeoutDecision.h"
#include "watchdog/TimeoutEventPublisher.h"
#include "watchdog/TimeoutPolicyEngine.h"
#include "watchdog/WatchdogAuditBridge.h"
#include "watchdog/WatchdogMetricsBridge.h"
#include "watchdog/WatchdogSnapshot.h"
#include "watchdog/WatchdogServiceFacade.h"
#include "support/TestAssertions.h"

namespace {

class RecordingTimeoutEventSink final
    : public dasall::infra::watchdog::ITimeoutEventSink {
 public:
  dasall::infra::watchdog::TimeoutEventDispatchResult publish_timeout_event(
      const dasall::infra::watchdog::TimeoutEvent& event) override {
    events.push_back(event);
    return dasall::infra::watchdog::TimeoutEventDispatchResult::success(
        "events://watchdog/" + std::to_string(events.size()));
  }

  std::vector<dasall::infra::watchdog::TimeoutEvent> events;
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

class RecordingMeter final : public dasall::infra::metrics::IMeter {
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
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://watchdog/integration");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://watchdog/provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://watchdog/provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://watchdog/provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

[[nodiscard]] dasall::infra::watchdog::WatchedEntityDescriptor make_descriptor(
    std::string entity_id,
    std::string owner_module,
    std::uint32_t timeout_ms = 15000,
    std::uint32_t grace_ms = 500) {
  return dasall::infra::watchdog::WatchedEntityDescriptor{
      .entity_id = std::move(entity_id),
      .entity_type = std::string("thread"),
      .owner_module = std::move(owner_module),
      .criticality = dasall::infra::watchdog::WatchdogEntityCriticality::Critical,
      .timeout_ms = timeout_ms,
      .grace_ms = grace_ms,
  };
}

[[nodiscard]] dasall::infra::watchdog::HeartbeatSample make_sample(
    std::string entity_id,
    std::uint64_t seq_no,
    std::int64_t heartbeat_ts,
    std::int64_t deadline_ts) {
  return dasall::infra::watchdog::HeartbeatSample{
      .entity_id = std::move(entity_id),
      .heartbeat_ts = heartbeat_ts,
      .deadline_ts = deadline_ts,
      .seq_no = seq_no,
  };
}

[[nodiscard]] std::shared_ptr<const dasall::infra::watchdog::TimeoutDecision>
make_prior_decision(dasall::infra::watchdog::WatchdogTimeoutLevel level,
                    std::uint32_t consecutive_miss) {
  return std::make_shared<dasall::infra::watchdog::TimeoutDecision>(
      dasall::infra::watchdog::TimeoutDecision{
          .entity_id = std::string("runtime.main_loop"),
          .timeout_level = level,
          .consecutive_miss = consecutive_miss,
          .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
          .evidence_ref = std::string("watchdog://decision/prior"),
      });
}

void test_watchdog_integration_closes_timeout_pipeline_with_observable_outputs() {
  using dasall::infra::watchdog::DeadlineWheel;
  using dasall::infra::watchdog::HeartbeatIngestor;
  using dasall::infra::watchdog::HeartbeatRegistry;
  using dasall::infra::watchdog::RecoveryRequestEmitter;
  using dasall::infra::watchdog::TimeoutEventPublisher;
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEngine;
  using dasall::infra::watchdog::WatchdogAuditBridge;
  using dasall::infra::watchdog::WatchdogMetricsBridge;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  WatchdogServiceConfig config;
  config.scan_interval_ms = 500;
  config.grace_ms = 500;
  config.consecutive_miss_threshold = 3;
  config.event_queue_size = 2;

  HeartbeatRegistry registry(config.max_entities);
  assert_true(registry.register_entity(
                  make_descriptor("runtime.main_loop", "runtime", config.timeout_ms, config.grace_ms))
                  .ok,
              "WatchdogIntegrationTest requires the runtime.main_loop entity to be registered before the timeout pipeline is exercised");

  HeartbeatIngestor ingestor(&registry, config.max_entities);
  const auto latest = make_sample("runtime.main_loop", 8, 1000, 1500);
  assert_true(ingestor.ingest(latest).ok,
              "WatchdogIntegrationTest requires one accepted heartbeat so the deadline wheel can surface a due candidate");

  DeadlineWheel wheel(config, &registry, &ingestor, nullptr);
  const auto due = wheel.tick_collect_due(2500);
  assert_true(due.ok && due.has_due_candidates() && due.due_candidates.size() == 1,
              "WatchdogIntegrationTest should surface exactly one due candidate once now_ts passes the frozen heartbeat deadline");

  TimeoutHistoryWindow history;
  history.recent_samples.push_back(
      std::make_shared<dasall::infra::watchdog::HeartbeatSample>(
          due.due_candidates.front().latest_sample));
  history.prior_decisions.push_back(
      make_prior_decision(WatchdogTimeoutLevel::Warning, 2));

  TimeoutPolicyEngine engine(config);
  const auto evaluation = engine.evaluate(
      std::make_shared<dasall::infra::watchdog::HeartbeatSample>(
          due.due_candidates.front().latest_sample),
      history);
  assert_true(evaluation.ok && evaluation.has_decision() &&
                  evaluation.decision->timeout_level == WatchdogTimeoutLevel::Critical &&
                  evaluation.decision->consecutive_miss == 3,
              "WatchdogIntegrationTest should escalate the due candidate into a critical timeout decision once the frozen miss threshold is reached");

  auto sink = std::make_shared<RecordingTimeoutEventSink>();
  TimeoutEventPublisher publisher(
      sink,
      config,
      dasall::infra::watchdog::TimeoutEventPublisherOptions{
          .default_trace_id = std::string("trace-integration-001"),
          .default_session_id = std::string("session-integration-001"),
          .default_task_id = std::string("task-integration-001"),
      });
  const auto publish = publisher.publish_timeout(*evaluation.decision);
  assert_true(publish.emitted && publish.is_valid() && sink->events.size() == 1 &&
                  sink->events.front().entity_id == "runtime.main_loop",
              "WatchdogIntegrationTest should publish the critical timeout event through the frozen timeout event sink boundary");

  auto audit_logger = std::make_shared<RecordingAuditLogger>();
  WatchdogAuditBridge audit_bridge(audit_logger);
  const auto audit = audit_bridge.write_timeout_audit(*evaluation.decision);
  assert_true(audit.emitted && audit.is_valid() && audit_logger->events.size() == 1 &&
                  audit_logger->events.front().action == "watchdog.timeout_detected",
              "WatchdogIntegrationTest should persist the critical timeout decision onto the watchdog audit bridge with the frozen action token");

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  WatchdogMetricsBridge metrics_bridge(provider, "edge_balanced");
  const auto metrics =
      metrics_bridge.record_timeout(*evaluation.decision, "runtime_thread", 2500);
  assert_true(metrics.emitted && meter->recorded_samples.size() == 1 &&
                  meter->recorded_samples.front().labels.stage == "timeout/runtime_thread" &&
                  meter->recorded_samples.front().labels.profile == "edge_balanced",
              "WatchdogIntegrationTest should emit watchdog timeout metrics with the frozen stage/profile labels after the decision is produced");

  RecoveryRequestEmitter emitter;
  const auto recovery = emitter.emit_recovery_hint(*evaluation.decision);
  assert_true(recovery.ok && recovery.request.has_required_fields() &&
                  recovery.request.target_ref == "runtime.main_loop" &&
                  recovery.request.suggested_action == "review_runtime_recovery_for_target",
              "WatchdogIntegrationTest should keep the timeout pipeline advisory-only by emitting a RecoveryHintRequest instead of executing recovery");
}

void test_watchdog_integration_facade_keeps_public_lifecycle_and_snapshot_consistent() {
  using dasall::infra::watchdog::HeartbeatSample;
  using dasall::infra::watchdog::WatchdogServiceConfig;
  using dasall::infra::watchdog::WatchdogServiceFacade;
  using dasall::tests::support::assert_true;

  WatchdogServiceFacade facade;
  WatchdogServiceConfig config;
  config.max_entities = 4;

  assert_true(facade.init(config).ok,
              "WatchdogIntegrationTest should initialize the public watchdog facade before lifecycle and snapshot interactions are exercised");
  assert_true(facade.register_entity(
                  make_descriptor("runtime.integration_loop", "runtime", config.timeout_ms, config.grace_ms))
                  .ok,
              "WatchdogIntegrationTest should register one watched entity through the public watchdog facade");
  assert_true(facade.start().ok,
              "WatchdogIntegrationTest should transition the public watchdog facade to started before heartbeats are injected");
  assert_true(facade.heartbeat(HeartbeatSample{
                  .entity_id = std::string("runtime.integration_loop"),
                  .heartbeat_ts = 1712577600000,
                  .deadline_ts = 1712577615000,
                  .seq_no = 1,
                }).ok,
              "WatchdogIntegrationTest should accept a public heartbeat through WatchdogServiceFacade once the lifecycle is started");

  const auto snapshot = facade.snapshot();
  assert_true(snapshot.ok && snapshot.has_snapshot() &&
                  snapshot.snapshot->has_consistent_counts() &&
                  snapshot.snapshot->total_entities == 1,
              "WatchdogIntegrationTest should preserve a consistent public snapshot after init, register, start, and heartbeat complete");
  assert_true(facade.stop(250).ok,
              "WatchdogIntegrationTest should complete the public lifecycle by stopping the facade with an explicit timeout budget");
}

}  // namespace

int main() {
  try {
    test_watchdog_integration_closes_timeout_pipeline_with_observable_outputs();
    test_watchdog_integration_facade_keeps_public_lifecycle_and_snapshot_consistent();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
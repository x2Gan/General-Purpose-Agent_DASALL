#include <exception>
#include <iostream>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audit/AuditService.h"
#include "audit/AuditMetricsBridge.h"
#include "audit/IAuditHealthProbe.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::string make_health_detail_ref(std::string_view suffix) {
  return std::string(dasall::infra::kAuditHealthDetailNamespace) + "/" +
         std::string(suffix);
}

dasall::infra::AuditEvent make_event(std::string ref_suffix) {
  const auto event_id = std::string("audit-event-") + ref_suffix;

  return dasall::infra::AuditEvent{
      .event_id = event_id,
      .action = std::string("diagnostics.export"),
      .actor = std::string("runtime"),
      .target = std::string("diag-bundle"),
      .outcome = dasall::infra::AuditOutcome::Succeeded,
      .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                       .ref = std::move(ref_suffix)},
      .side_effects = {"bundle_written"},
      .timestamp = 1712217600000,
  };
}

dasall::infra::AuditContext make_context() {
  return dasall::infra::AuditContext{};
}

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities_.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity&) override {
    return std::nullopt;
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples_.push_back(sample);
    if (scripted_record_results_.empty()) {
      return dasall::infra::metrics::MetricsOperationStatus::success(
          "metrics://integration-record");
    }

    auto result = scripted_record_results_.front();
    scripted_record_results_.pop_front();
    return result;
  }

  void push_record_result(
      dasall::infra::metrics::MetricsOperationStatus result) {
    scripted_record_results_.push_back(std::move(result));
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricIdentity>&
  created_identities() const {
    return created_identities_;
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricSample>&
  recorded_samples() const {
    return recorded_samples_;
  }

 private:
  std::vector<dasall::infra::metrics::MetricIdentity> created_identities_;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples_;
  std::deque<dasall::infra::metrics::MetricsOperationStatus>
      scripted_record_results_;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://integration-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope_ = scope;
    ++get_meter_call_total_;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://integration-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://integration-provider-shutdown");
  }

  [[nodiscard]] const dasall::infra::metrics::MeterScope& last_scope() const {
    return last_scope_;
  }

  [[nodiscard]] std::uint64_t get_meter_call_total() const {
    return get_meter_call_total_;
  }

 private:
  std::shared_ptr<RecordingMeter> meter_;
  dasall::infra::metrics::MeterScope last_scope_{};
  std::uint64_t get_meter_call_total_ = 0;
};

class AuditServiceBackedHealthProbe final : public dasall::infra::audit::IAuditHealthProbe {
 public:
  AuditServiceBackedHealthProbe(const dasall::infra::audit::AuditService& service,
                                const dasall::infra::audit::AuditMetricsBridge& metrics_bridge,
                                std::int64_t sampled_at_unix_ms)
      : service_(service),
        metrics_bridge_(metrics_bridge),
        sampled_at_unix_ms_(sampled_at_unix_ms) {}

  [[nodiscard]] dasall::infra::AuditHealthStatus evaluate() const override {
    const bool metrics_bridge_degraded = metrics_bridge_.is_degraded();
    const auto lifecycle_state = service_.lifecycle_state_name();
    if (lifecycle_state != "started") {
      const bool stopped = lifecycle_state == "stopped";
      return dasall::infra::AuditHealthStatus{
          .state = dasall::infra::AuditHealthState::Unavailable,
          .last_failure_reason =
              std::string(stopped ? "service_stopped" : "service_not_started"),
          .detail_ref = make_health_detail_ref(
              stopped ? "unavailable/service_stopped"
                      : "unavailable/service_not_started"),
          .error_code = dasall::contracts::ResultCode::RuntimeRetryExhausted,
          .sampled_at_unix_ms = sampled_at_unix_ms_,
          .fallback_active = false,
          .metrics_bridge_degraded = metrics_bridge_degraded,
      };
    }

    if (service_.is_degraded()) {
      return dasall::infra::AuditHealthStatus{
          .state = dasall::infra::AuditHealthState::Degraded,
          .last_failure_reason = std::string("fallback_active"),
          .detail_ref = make_health_detail_ref("degraded/fallback_active"),
          .error_code = std::nullopt,
          .sampled_at_unix_ms = sampled_at_unix_ms_,
          .fallback_active = true,
          .metrics_bridge_degraded = metrics_bridge_degraded,
      };
    }

    if (metrics_bridge_degraded) {
      return dasall::infra::AuditHealthStatus{
          .state = dasall::infra::AuditHealthState::Degraded,
          .last_failure_reason = std::string("metrics_bridge_degraded"),
          .detail_ref = make_health_detail_ref("degraded/metrics_bridge"),
          .error_code = std::nullopt,
          .sampled_at_unix_ms = sampled_at_unix_ms_,
          .fallback_active = false,
          .metrics_bridge_degraded = true,
      };
    }

    return dasall::infra::AuditHealthStatus{
        .state = dasall::infra::AuditHealthState::Ready,
        .last_failure_reason = std::string(),
        .detail_ref = make_health_detail_ref("ready"),
        .error_code = std::nullopt,
        .sampled_at_unix_ms = sampled_at_unix_ms_,
        .fallback_active = false,
        .metrics_bridge_degraded = false,
    };
  }

 private:
  const dasall::infra::audit::AuditService& service_;
  const dasall::infra::audit::AuditMetricsBridge& metrics_bridge_;
  std::int64_t sampled_at_unix_ms_ = 0;
};

void test_audit_health_integration_reports_ready_after_successful_write() {
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditMetricKind;
  using dasall::infra::audit::AuditMetricSignal;
  using dasall::infra::audit::AuditMetricsBridge;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  AuditMetricsBridge metrics_bridge(provider, "desktop_full");
  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 2, .fallback_capacity = 1}).ok,
              "audit health integration should initialize the service before evaluating health");
  assert_true(service.start().ok,
              "audit health integration should start the service before evaluating health");

  const auto write_result = service.write_audit(make_event("health-ready-001"), make_context());
  assert_true(write_result.is_success(),
              "audit health integration should keep the baseline write on the primary path");

  const auto metrics_result = metrics_bridge.emit(AuditMetricSignal{
      .kind = AuditMetricKind::WriteTotal,
      .value = 1.0,
      .ts_unix_ms = 1712217601000,
      .stage = std::string("write"),
      .outcome = std::string("success"),
      .audit_error_code = std::nullopt,
  });
  assert_true(metrics_result.emitted && metrics_result.has_consistent_state(),
              "audit health integration should emit the frozen audit_write_total metric through the metrics bridge");
  assert_true(metrics_bridge.has_active_meter() &&
                  metrics_bridge.instruments_registered() &&
                  !metrics_bridge.is_degraded(),
              "successful bridge emissions should keep the audit metrics bridge healthy");
  assert_equal(std::string("infra.audit"),
               provider->last_scope().name,
               "audit health integration should request the frozen infra.audit meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope().version,
               "audit health integration should preserve the frozen audit meter scope version");
  assert_equal(7,
               static_cast<int>(meter->created_identities().size()),
               "audit health integration should register the seven frozen audit metric families on first use");
  assert_equal(1,
               static_cast<int>(meter->recorded_samples().size()),
               "audit health integration should record one sample for one successful bridge emission");
  assert_equal(std::string("audit"),
               meter->recorded_samples().front().labels.module,
               "audit health integration should pin module=audit in metric labels");

  AuditServiceBackedHealthProbe probe(service, metrics_bridge, 1712217601000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Ready,
              "audit health integration should expose Ready after a successful primary-path write");
}

void test_audit_health_integration_reports_degraded_when_fallback_is_active() {
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditMetricKind;
  using dasall::infra::audit::AuditMetricSignal;
  using dasall::infra::audit::AuditMetricsBridge;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  AuditMetricsBridge metrics_bridge(provider, "edge_balanced");
  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit health integration should initialize before degraded-path validation");
  assert_true(service.start().ok,
              "audit health integration should start before degraded-path validation");

  assert_true(service.write_audit(make_event("health-degraded-001"), make_context()).is_success(),
              "audit health integration should fill the primary slot before fallback is needed");
  const auto degraded_write = service.write_audit(make_event("health-degraded-002"), make_context());
  assert_true(degraded_write.is_degraded_success(),
              "audit health integration should route the second event through fallback when primary capacity is exhausted");

  const auto metrics_result = metrics_bridge.emit(AuditMetricSignal{
      .kind = AuditMetricKind::FallbackTotal,
      .value = 1.0,
      .ts_unix_ms = 1712217602000,
      .stage = std::string("fallback"),
      .outcome = std::string("degraded"),
      .audit_error_code = std::nullopt,
  });
  assert_true(metrics_result.emitted && !metrics_bridge.is_degraded(),
              "fallback metrics should emit successfully without turning the bridge degraded");

  AuditServiceBackedHealthProbe probe(service, metrics_bridge, 1712217602000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Degraded,
              "audit health integration should expose Degraded when fallback stays active");
  assert_true(snapshot.fallback_active && snapshot.last_failure_reason == "fallback_active",
              "audit health integration should surface fallback_active as the degraded reason");
}

void test_audit_health_integration_reports_metrics_bridge_degraded_without_promoting_unavailable() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditMetricKind;
  using dasall::infra::audit::AuditMetricSignal;
  using dasall::infra::audit::AuditMetricsBridge;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  meter->push_record_result(MetricsOperationStatus::failure(
      ResultCode::ProviderTimeout,
      "metrics exporter timed out",
      "metrics.record",
      "RecordingMeter"));
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  AuditMetricsBridge metrics_bridge(provider, "desktop_full");
  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit health integration should initialize before metrics degraded validation");
  assert_true(service.start().ok,
              "audit health integration should start before metrics degraded validation");

  const auto write_result = service.write_audit(make_event("health-metrics-001"), make_context());
  assert_true(write_result.is_success(),
              "audit health integration should keep the audit write result successful before metrics degradation is injected");

  const auto metrics_result = metrics_bridge.emit(AuditMetricSignal{
      .kind = AuditMetricKind::QueueDepth,
      .value = 1.0,
      .ts_unix_ms = 1712217603000,
      .stage = std::string("health"),
      .outcome = std::string("degraded"),
      .audit_error_code = std::nullopt,
  });
  assert_true(!metrics_result.emitted && metrics_result.bridge_degraded,
              "provider/exporter failures should only degrade the audit metrics bridge instead of fabricating a successful emission");
  assert_true(metrics_bridge.is_degraded() &&
                  metrics_bridge.last_metrics_error_code() ==
                      dasall::infra::metrics::MetricsErrorCode::ExportFailure,
              "provider timeout should map to MET_E_EXPORT_FAILURE and keep the bridge degraded");

  AuditServiceBackedHealthProbe probe(service, metrics_bridge, 1712217603000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Degraded,
              "audit health integration should keep metrics bridge degradation at the Degraded state");
  assert_true(snapshot.metrics_bridge_degraded && !snapshot.error_code.has_value(),
              "audit health integration should expose metrics bridge degradation without forcing an unavailable error code");
}

void test_audit_health_integration_reports_unavailable_when_service_is_stopped() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditHealthState;
  using dasall::infra::audit::AuditMetricsBridge;
  using dasall::infra::audit::AuditService;
  using dasall::infra::audit::AuditServiceConfig;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  AuditMetricsBridge metrics_bridge(provider, "desktop_full");
  AuditService service;
  assert_true(service.init(AuditServiceConfig{.primary_capacity = 1, .fallback_capacity = 1}).ok,
              "audit health integration should initialize before stopped-state validation");
  assert_true(service.start().ok,
              "audit health integration should start before stopped-state validation");
  assert_true(service.stop().ok,
              "audit health integration should stop cleanly before unavailable-state validation");

  AuditServiceBackedHealthProbe probe(service, metrics_bridge, 1712217604000);
  const auto snapshot = probe.evaluate();
  assert_true(snapshot.has_consistent_state() && snapshot.state == AuditHealthState::Unavailable,
              "audit health integration should expose Unavailable after the service has stopped");
  assert_true(snapshot.error_code == ResultCode::RuntimeRetryExhausted &&
                  snapshot.last_failure_reason == "service_stopped",
              "audit health integration should keep stopped-state failures observable through the frozen unavailable semantics");
}

}  // namespace

int main() {
  try {
    test_audit_health_integration_reports_ready_after_successful_write();
    test_audit_health_integration_reports_degraded_when_fallback_is_active();
    test_audit_health_integration_reports_metrics_bridge_degraded_without_promoting_unavailable();
    test_audit_health_integration_reports_unavailable_when_service_is_stopped();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

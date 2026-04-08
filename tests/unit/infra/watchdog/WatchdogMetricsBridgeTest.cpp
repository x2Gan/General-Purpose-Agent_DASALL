#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "metrics/IMeter.h"
#include "metrics/IMetricsProvider.h"
#include "metrics/MetricTypes.h"
#include "watchdog/TimeoutDecision.h"
#include "watchdog/WatchdogMetricsBridge.h"
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
        "metrics://watchdog/record");
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
  std::shared_ptr<ScriptedMeter> meter_;
};

[[nodiscard]] bool has_identity(
    std::vector<dasall::infra::metrics::MetricIdentity> identities,
    std::string_view name,
    dasall::infra::metrics::MetricType type,
    std::string_view unit) {
  return std::any_of(identities.begin(), identities.end(), [&](const auto& identity) {
    return identity.name == name && identity.type == type && identity.unit == unit;
  });
}

[[nodiscard]] dasall::infra::watchdog::TimeoutDecision make_decision(
    dasall::infra::watchdog::WatchdogTimeoutLevel timeout_level,
    std::uint32_t consecutive_miss) {
  return dasall::infra::watchdog::TimeoutDecision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = timeout_level,
      .consecutive_miss = consecutive_miss,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::string("watchdog://timeout/runtime.main_loop"),
  };
}

void test_watchdog_metrics_bridge_registers_frozen_metric_families_and_timeout_labels() {
  using dasall::infra::metrics::MetricType;
  using dasall::infra::watchdog::WatchdogMetricsBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<ScriptedMeter>();
  auto provider = std::make_shared<ScriptedProvider>(meter);
  WatchdogMetricsBridge bridge(provider, "edge_balanced");

  const auto entities_result = bridge.record_entities_total(12, 1712577600000);
  const auto timeout_result = bridge.record_timeout(
      make_decision(WatchdogTimeoutLevel::Critical, 3),
      "runtime_thread",
      1712577600001);

  assert_true(entities_result.emitted && timeout_result.emitted,
              "WatchdogMetricsBridge should emit entities_total and timeout_total samples once provider and meter are available");
  assert_equal(std::string("infra.watchdog"),
               provider->last_scope.name,
               "WatchdogMetricsBridge should request the frozen infra.watchdog meter scope");
  assert_equal(std::string("v1"),
               provider->last_scope.version,
               "WatchdogMetricsBridge should preserve the frozen watchdog meter scope version");
  assert_equal(7, static_cast<int>(meter->created_identities.size()),
               "WatchdogMetricsBridge should register exactly the seven frozen watchdog metric families on first emit");
  assert_true(has_identity(meter->created_identities,
                           "infra_watchdog_entities_total",
                           MetricType::Gauge,
                           "1") &&
                  has_identity(meter->created_identities,
                               "infra_watchdog_timeout_total",
                               MetricType::Counter,
                               "1") &&
                  has_identity(meter->created_identities,
                               "infra_watchdog_scan_lag_ms",
                               MetricType::Gauge,
                               "ms"),
              "WatchdogMetricsBridge should preserve the frozen name/type/unit contract for representative watchdog metric families");
  assert_equal(2, static_cast<int>(meter->recorded_samples.size()),
               "WatchdogMetricsBridge should record one sample per accepted watchdog emission call");
  assert_true(meter->recorded_samples.back().labels.module == "watchdog" &&
                  meter->recorded_samples.back().labels.stage == "timeout/runtime_thread" &&
                  meter->recorded_samples.back().labels.profile == "edge_balanced" &&
                  meter->recorded_samples.back().labels.outcome == "critical" &&
                  meter->recorded_samples.back().labels.error_code == "none",
              "WatchdogMetricsBridge should project timeout metric labels using the frozen watchdog stage/outcome/error_code mapping");
}

void test_watchdog_metrics_bridge_exposes_all_seven_metric_entrypoints() {
  using dasall::infra::watchdog::WatchdogMetricsBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<ScriptedMeter>();
  auto provider = std::make_shared<ScriptedProvider>(meter);
  WatchdogMetricsBridge bridge(provider, "desktop_full");

  assert_true(bridge.record_entities_total(8, 1712577600100).emitted,
              "WatchdogMetricsBridge should expose entities_total as a direct sampling entrypoint");
  assert_true(bridge.record_heartbeat_ingest("runtime_thread", 1712577600101).emitted,
              "WatchdogMetricsBridge should expose heartbeat_ingest_total as a direct sampling entrypoint");
  assert_true(bridge.record_timeout(make_decision(WatchdogTimeoutLevel::Fatal, 4),
                                    "runtime_thread",
                                    1712577600102)
                  .emitted,
              "WatchdogMetricsBridge should expose timeout_total as a direct sampling entrypoint");
  assert_true(bridge.record_consecutive_miss("runtime.main_loop", 4, 1712577600103)
                  .emitted,
              "WatchdogMetricsBridge should expose consecutive_miss as a direct sampling entrypoint");
  assert_true(bridge.record_scan_lag(1200, 1712577600104, true).emitted,
              "WatchdogMetricsBridge should expose scan_lag_ms as a direct sampling entrypoint");
  assert_true(bridge.record_publish_fail(1712577600105).emitted,
              "WatchdogMetricsBridge should expose event_publish_fail_total as a direct sampling entrypoint");
  assert_true(bridge.record_safe_mode(1712577600106).emitted,
              "WatchdogMetricsBridge should expose safe_mode_total as a direct sampling entrypoint");

  assert_equal(7, static_cast<int>(meter->recorded_samples.size()),
               "WatchdogMetricsBridge should emit one metric sample for each of the seven frozen watchdog metric families");
  assert_true(meter->recorded_samples[3].labels.stage == "consecutive_miss/runtime.main_loop" &&
                  meter->recorded_samples[4].labels.stage == "scan_lag" &&
                  meter->recorded_samples[4].labels.error_code ==
                      "INF_E_WATCHDOG_SCAN_OVERDUE" &&
                  meter->recorded_samples[5].labels.stage == "event_publish_fail" &&
                  meter->recorded_samples[5].labels.outcome == "failure" &&
                  meter->recorded_samples[5].labels.error_code ==
                      "INF_E_WATCHDOG_EVENT_PUBLISH_FAIL" &&
                  meter->recorded_samples[6].labels.stage == "safe_mode" &&
                  meter->recorded_samples[6].labels.outcome == "degraded",
              "WatchdogMetricsBridge should keep consecutive miss, scan lag, publish failure and safe mode labels inside the frozen watchdog contract");
}

void test_watchdog_metrics_bridge_rejects_invalid_timeout_inputs() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::watchdog::WatchdogMetricsBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<ScriptedMeter>();
  auto provider = std::make_shared<ScriptedProvider>(meter);
  WatchdogMetricsBridge bridge(provider);

  const auto result = bridge.record_timeout(
      make_decision(WatchdogTimeoutLevel::Critical, 3),
      std::string(),
      1712577600200);

  assert_true(!result.emitted && result.metrics_error_code == MetricsErrorCode::ConfigInvalid,
              "WatchdogMetricsBridge should reject timeout metrics that do not provide a valid entity_type label token");
  assert_true(meter->recorded_samples.empty(),
              "WatchdogMetricsBridge should not emit any metric sample when local watchdog label guards fail");
}

void test_watchdog_metrics_bridge_surfaces_provider_not_ready_without_success_fabrication() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::watchdog::WatchdogMetricsBridge;
  using dasall::tests::support::assert_true;

  WatchdogMetricsBridge bridge(nullptr, "desktop_full");

  const auto result = bridge.record_safe_mode(1712577600300);

  assert_true(!result.emitted && result.bridge_degraded && result.has_consistent_state(),
              "WatchdogMetricsBridge should keep provider-not-ready visible as a local degraded bridge result");
  assert_true(result.metrics_error_code == MetricsErrorCode::ProviderNotReady &&
                  bridge.is_degraded() && !bridge.has_active_meter(),
              "WatchdogMetricsBridge should retain provider-not-ready in bridge-local status instead of fabricating a successful emit");
}

}  // namespace

int main() {
  try {
    test_watchdog_metrics_bridge_registers_frozen_metric_families_and_timeout_labels();
    test_watchdog_metrics_bridge_exposes_all_seven_metric_entrypoints();
    test_watchdog_metrics_bridge_rejects_invalid_timeout_inputs();
    test_watchdog_metrics_bridge_surfaces_provider_not_ready_without_success_fabrication();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
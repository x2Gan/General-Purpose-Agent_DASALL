#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] bool has_identity(std::vector<dasall::infra::metrics::MetricIdentity> identities,
                                std::string_view name,
                                dasall::infra::metrics::MetricType type,
                                std::string_view unit) {
  return std::any_of(identities.begin(), identities.end(), [&](const auto& identity) {
    return identity.name == name && identity.type == type && identity.unit == unit;
  });
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

}  // namespace

int main() {
  try {
    test_diagnostics_metrics_bridge_emits_frozen_metric_families_with_scope_and_labels();
    test_diagnostics_metrics_bridge_rejects_non_whitelist_stage_and_error_code();
    test_diagnostics_metrics_bridge_surfaces_provider_failures_without_recursive_blocking();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
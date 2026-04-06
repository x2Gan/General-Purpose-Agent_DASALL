#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "metrics/MetricsConfigPolicy.h"
#include "metrics/MetricsErrors.h"
#include "metrics/MetricsExporterAdapter.h"
#include "metrics/MetricsRecovery.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class CapturingRecoveryLogHook final
    : public dasall::infra::metrics::IMetricsRecoveryLogHook {
 public:
  dasall::infra::metrics::MetricsOperationStatus write_recovery_event(
      const dasall::infra::metrics::MetricsRecoveryEvent& event) override {
    events.push_back(event);
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics-recovery-hook://captured");
  }

  std::vector<dasall::infra::metrics::MetricsRecoveryEvent> events;
};

[[nodiscard]] dasall::infra::metrics::MetricsExporterAdapter make_timeouting_adapter() {
  using dasall::infra::metrics::MetricsConfigPatch;
  using dasall::infra::metrics::MetricsConfigPolicy;
  using dasall::infra::metrics::MetricsExporterAdapter;

  MetricsConfigPolicy policy;
  MetricsConfigPatch profile_patch;
  profile_patch.exporter_type = std::string("prom_text");
  profile_patch.exporter_timeout_ms = 1U;
  return MetricsExporterAdapter(
      policy.merge(profile_patch, MetricsConfigPatch{}, MetricsConfigPatch{}));
}

[[nodiscard]] dasall::infra::metrics::MetricExportBatch make_batch(std::string batch_id,
                                                                   std::size_t sample_count,
                                                                   std::string exporter_type) {
  return dasall::infra::metrics::MetricExportBatch{
      .batch_id = std::move(batch_id),
      .sample_count = sample_count,
      .exporter_type = std::move(exporter_type),
  };
}

void test_metrics_recovery_enters_degraded_mode_after_consecutive_export_failures() {
  using dasall::infra::metrics::MetricsRecovery;
  using dasall::tests::support::assert_true;

  auto log_hook = std::make_shared<CapturingRecoveryLogHook>();
  auto adapter = make_timeouting_adapter();
  MetricsRecovery recovery(2U, log_hook);

  const auto first_failure =
      adapter.export_batch(make_batch("metrics-batch://timeout/1", 5U, "prom_text"));
  const auto first_observation =
      recovery.observe_export_result(first_failure, adapter.module_snapshot());

  assert_true(first_observation.ok && !recovery.is_degraded() &&
                  recovery.consecutive_failure_total() == 1U &&
                  log_hook->events.empty(),
              "MetricsRecovery should observe the first exporter failure without entering degraded mode before the configured threshold is reached");

  const auto second_failure =
      adapter.export_batch(make_batch("metrics-batch://timeout/2", 5U, "prom_text"));
  const auto second_observation =
      recovery.observe_export_result(second_failure, adapter.module_snapshot());

  assert_true(second_observation.ok && recovery.is_degraded() &&
                  recovery.degrade_enter_total() == 1U &&
                  recovery.module_snapshot().degraded && recovery.last_event().has_value() &&
                  recovery.last_event()->action == "enter_degraded" &&
                  log_hook->events.size() == 1U && log_hook->events.back().is_valid(),
              "MetricsRecovery should enter degraded mode observably once continuous exporter failures cross the configured threshold");
}

void test_metrics_recovery_returns_to_healthy_after_successful_export() {
  using dasall::infra::metrics::MetricsRecovery;
  using dasall::tests::support::assert_true;

  auto log_hook = std::make_shared<CapturingRecoveryLogHook>();
  auto adapter = make_timeouting_adapter();
  MetricsRecovery recovery(2U, log_hook);

  (void)recovery.observe_export_result(
      adapter.export_batch(make_batch("metrics-batch://timeout/3", 5U, "prom_text")),
      adapter.module_snapshot());
  (void)recovery.observe_export_result(
      adapter.export_batch(make_batch("metrics-batch://timeout/4", 5U, "prom_text")),
      adapter.module_snapshot());

  const auto success_result =
      adapter.export_batch(make_batch("metrics-batch://recovered/1", 1U, "noop"));
  const auto recovery_result =
      recovery.observe_export_result(success_result, adapter.module_snapshot());

  assert_true(recovery_result.ok && !recovery.is_degraded() &&
                  recovery.recovery_success_total() == 1U &&
                  recovery.consecutive_failure_total() == 0U &&
                  recovery.last_event().has_value() &&
                  recovery.last_event()->action == "recover_to_healthy" &&
                  log_hook->events.size() == 2U && !recovery.module_snapshot().degraded,
              "MetricsRecovery should clear degraded mode and emit a recovery event after the exporter returns to a healthy success path");
}

void test_metrics_recovery_rejects_invalid_transition_inputs() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsRecovery;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::tests::support::assert_true;

  MetricsRecovery recovery;
  const auto result = recovery.enter_degraded(MetricsErrorCode::ExportFailure,
                                              std::string(),
                                              dasall::infra::metrics::MetricsModuleSnapshot{});

  assert_true(!result.ok && result.references_only_contract_error_types() &&
                  result.result_code ==
                      map_metrics_error_code(MetricsErrorCode::ConfigInvalid).result_code,
              "MetricsRecovery should reject invalid degraded-mode transitions when the reason or snapshot is missing");
}

}  // namespace

int main() {
  try {
    test_metrics_recovery_enters_degraded_mode_after_consecutive_export_failures();
    test_metrics_recovery_returns_to_healthy_after_successful_export();
    test_metrics_recovery_rejects_invalid_transition_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "logging/ILogger.h"
#include "metrics/MetricsBridgeEvent.h"
#include "metrics/MetricsConfigPolicy.h"
#include "metrics/MetricsExporterAdapter.h"
#include "metrics/MetricsLoggingBridge.h"
#include "metrics/MetricsRecovery.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    events.push_back(event);
    if (!scripted_results.empty()) {
      const auto result = scripted_results.front();
      scripted_results.pop_front();
      return result;
    }

    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    last_level = level;
  }

  std::deque<dasall::infra::logging::LogWriteResult> scripted_results;
  std::vector<dasall::infra::logging::LogEvent> events;
  dasall::infra::logging::LogLevel last_level =
      dasall::infra::logging::LogLevel::Info;
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

void test_metrics_logging_bridge_acts_as_recovery_hook_and_emits_structured_logs() {
  using dasall::infra::metrics::MetricsLoggingBridge;
  using dasall::infra::metrics::MetricsRecovery;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedLogger>();
  auto bridge = std::make_shared<MetricsLoggingBridge>(logger);
  auto adapter = make_timeouting_adapter();
  MetricsRecovery recovery(2U, bridge);

  (void)recovery.observe_export_result(
      adapter.export_batch(make_batch("metrics-batch://timeout/1", 5U, "prom_text")),
      adapter.module_snapshot());
  const auto second_result = recovery.observe_export_result(
      adapter.export_batch(make_batch("metrics-batch://timeout/2", 5U, "prom_text")),
      adapter.module_snapshot());
  const auto status = bridge->get_status();

  assert_true(second_result.ok,
              "metrics recovery should keep its best-effort transition result even when the bridge is responsible for external logging");
  assert_equal(1, static_cast<int>(logger->events.size()),
               "metrics logging bridge should emit exactly one structured log entry when recovery enters degraded mode");
  assert_true(status.is_valid() && status.emitted_total == 1 && !status.degraded,
              "metrics logging bridge should remain healthy after a successful structured log dispatch");
  assert_equal(std::string("metrics"),
               logger->events.back().module,
               "metrics logging bridge should pin module=metrics on emitted structured log events");
  assert_equal(std::string("enter_degraded"),
               logger->events.back().attrs.at("action"),
               "metrics logging bridge should expose the recovery action as a stable structured log attribute");
  assert_equal(std::string("true"),
               logger->events.back().attrs.at("degraded"),
               "metrics logging bridge should expose degraded=true when the recovery hook records a degraded transition");
  assert_true(logger->events.back().attrs.at("error_code").find("MET_E_EXPORT_TIMEOUT") !=
                  std::string::npos,
              "metrics logging bridge should surface the normalized metrics error token instead of raw exporter implementation details");
}

void test_metrics_logging_bridge_rejects_invalid_bridge_payloads_before_logging() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsBridgeEvent;
  using dasall::infra::metrics::MetricsBridgeEventKind;
  using dasall::infra::metrics::MetricsBridgeEventOutcome;
  using dasall::infra::metrics::MetricsLoggingBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedLogger>();
  MetricsLoggingBridge bridge(logger);

  const auto result = bridge.write_log_event(MetricsBridgeEvent{
      .kind = MetricsBridgeEventKind::ConfigChange,
      .action = std::string("config_changed"),
      .stage = std::string("metrics.config.apply"),
      .outcome = MetricsBridgeEventOutcome::Success,
      .reason = std::string("metrics profile switched to desktop_full"),
      .error_code = std::nullopt,
      .module_snapshot = {.queue_depth = 0,
                          .guard_reject_total = 0,
                          .exporter_state = std::string("noop"),
                          .degraded = false},
      .context = {},
      .detail_ref = std::string("metrics://config/changed/desktop_full"),
      .config_version = std::string("desktop_full:v2"),
      .previous_config_version = std::string(),
      .timestamp_ms = 1712400000000,
  });
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.has_consistent_state(),
              "metrics logging bridge should reject invalid config-change payloads before they reach ILogger");
  assert_true(result.write_result.result_code == ResultCode::ValidationFieldMissing,
              "metrics logging bridge should map invalid bridge payloads to the frozen validation failure category");
  assert_true(status.is_valid() && status.degraded && status.emit_failures == 1,
              "metrics logging bridge should retain a degraded local status after rejecting an invalid payload");
  assert_true(logger->events.empty(),
              "metrics logging bridge should not dispatch any log entry when the local payload guard fails");
}

void test_metrics_logging_bridge_keeps_recovery_hook_best_effort_when_logger_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsLoggingBridge;
  using dasall::infra::metrics::MetricsModuleSnapshot;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::infra::metrics::MetricsRecoveryEvent;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedLogger>();
  logger->scripted_results.push_back(
      dasall::infra::logging::LogWriteResult::failure(
          ResultCode::ProviderTimeout,
          "structured sink unavailable",
          "metrics.logging.bridge",
          "ScriptedLogger"));

  MetricsLoggingBridge bridge(logger);
  const auto hook_result = bridge.write_recovery_event(MetricsRecoveryEvent{
      .action = std::string("enter_degraded"),
      .stage = std::string("metrics.recovery.enter_degraded"),
      .reason = std::string("metrics exporter timed out twice consecutively"),
      .error_code = dasall::infra::metrics::MetricsErrorCode::ExportTimeout,
      .module_snapshot = MetricsModuleSnapshot{
          .queue_depth = 3,
          .guard_reject_total = 1,
          .exporter_state = std::string("noop"),
          .degraded = true,
      },
      .consecutive_failure_total = 2,
      .degrade_enter_total = 1,
      .recovery_success_total = 0,
  });
  const auto status = bridge.get_status();

  assert_true(hook_result.ok,
              "metrics logging bridge should keep MetricsRecovery on a best-effort path even when the logger sink fails");
  assert_equal(1, static_cast<int>(logger->events.size()),
               "metrics logging bridge should still attempt one structured log write before degrading itself");
  assert_true(status.is_valid() && status.degraded && status.emit_failures == 1 &&
                  status.last_error_code == ResultCode::ProviderTimeout,
              "metrics logging bridge should retain the sink failure locally for later diagnostics instead of reflecting it back into the recovery state machine");
}

}  // namespace

int main() {
  try {
    test_metrics_logging_bridge_acts_as_recovery_hook_and_emits_structured_logs();
    test_metrics_logging_bridge_rejects_invalid_bridge_payloads_before_logging();
    test_metrics_logging_bridge_keeps_recovery_hook_best_effort_when_logger_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
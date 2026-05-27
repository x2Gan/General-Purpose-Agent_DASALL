#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "logging/LoggingFacade.h"
#include "logging/LoggingMetricsBridge.h"
#include "support/TestAssertions.h"

namespace {

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    samples_.push_back(sample);
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://recorded");
  }

  [[nodiscard]] const std::vector<dasall::infra::metrics::MetricSample>& samples()
      const {
    return samples_;
  }

 private:
  std::vector<dasall::infra::metrics::MetricSample> samples_;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope&) override {
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://provider-shutdown");
  }

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

class ScriptedDispatchBackend final
    : public dasall::infra::logging::ILogDispatchBackend {
 public:
  void push_dispatch_result(dasall::infra::logging::LogWriteResult result) {
    dispatch_results_.push_back(std::move(result));
  }

  dasall::infra::logging::LogWriteResult dispatch(
      const dasall::infra::logging::LogEvent&) override {
    if (dispatch_results_.empty()) {
      return dasall::infra::logging::LogWriteResult::success();
    }

    auto result = dispatch_results_.front();
    dispatch_results_.pop_front();
    return result;
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

 private:
  std::deque<dasall::infra::logging::LogWriteResult> dispatch_results_;
};

class FailingRecoverySink final : public dasall::infra::logging::ILogRecoverySink {
 public:
  dasall::infra::logging::LogWriteResult write(
      const dasall::infra::logging::LogEvent&) override {
    return dasall::infra::logging::LogWriteResult::failure(
        dasall::infra::logging::map_logging_error_code(
            dasall::infra::logging::LoggingErrorCode::SinkIo)
            .result_code,
        "fallback sink unavailable",
        "logging.recovery.fallback",
        "FailingRecoverySink");
  }
};

[[nodiscard]] dasall::infra::logging::LogContext make_context() {
  return dasall::infra::logging::LogContext{
      .request_id = std::string("req-log-main-chain-001"),
      .session_id = std::string("session-log-main-chain-001"),
      .trace_id = std::string("trace-log-main-chain-001"),
      .task_id = std::string("task-log-main-chain-001"),
      .parent_task_id = std::string("parent-log-main-chain-001"),
      .lease_id = std::string("lease-log-main-chain-001"),
  };
}

[[nodiscard]] dasall::infra::logging::LogEvent make_event(
    std::string message,
    std::int64_t timestamp_ms = 1712400001000LL) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {{"event_name", "logging.main_chain"}},
      .ts = timestamp_ms,
  };
}

[[nodiscard]] const dasall::infra::metrics::MetricSample* find_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    std::string_view metric_name,
    std::string_view stage,
    std::string_view outcome) {
  for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
    if (it->identity_ref.name == metric_name && it->labels.stage == stage &&
        it->labels.outcome == outcome) {
      return &(*it);
    }
  }

  return nullptr;
}

[[nodiscard]] std::size_t count_samples_named(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    std::string_view metric_name) {
  std::size_t total = 0U;
  for (const auto& sample : samples) {
    if (sample.identity_ref.name == metric_name) {
      ++total;
    }
  }

  return total;
}

void test_logging_metrics_bridge_main_chain_emits_accepted_and_flush_samples() {
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);

  LoggingFacade facade(std::make_unique<ScriptedDispatchBackend>());
  assert_true(facade.init(make_context()).ok,
              "logging main-chain metrics test should initialize the facade before emitting samples");
  facade.attach_metrics_bridge(
      std::make_shared<LoggingMetricsBridge>(provider, "desktop_full"),
      1U);

  const auto write_result = facade.log(make_event("accepted main-chain write"));
  const auto flush_result = facade.flush(LogFlushDeadline{.timeout_ms = 250});

  assert_true(write_result.ok && flush_result.ok,
              "successful log and flush calls should stay on the accepted main chain path");
  assert_true(
      find_sample(meter->samples(), "logging_write_total", "write", "success") != nullptr,
      "accepted main-chain writes should emit logging_write_total(stage=write,outcome=success)");
  assert_true(
      find_sample(meter->samples(),
                  "logging_flush_latency_ms",
                  "flush",
                  "success") != nullptr,
      "successful flush attempts should emit logging_flush_latency_ms(stage=flush,outcome=success)");
  assert_equal(2,
               static_cast<int>(count_samples_named(meter->samples(),
                                                    "logging_queue_depth")),
               "accepted write and flush should each refresh logging_queue_depth(stage=queue)");
}

void test_logging_metrics_bridge_main_chain_emits_drop_samples_for_queue_saturation() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);
  auto backend = std::make_unique<ScriptedDispatchBackend>();
  backend->push_dispatch_result(dasall::infra::logging::LogWriteResult::failure(
      ResultCode::RuntimeRetryExhausted,
      "queue full",
      "logging.dispatch",
      "ScriptedDispatchBackend"));

  LoggingFacade facade(std::move(backend));
  assert_true(facade.init(make_context()).ok,
              "queue saturation metrics test should initialize the facade first");
  facade.attach_metrics_bridge(
      std::make_shared<LoggingMetricsBridge>(provider, "desktop_full"),
      1U);

  const auto write_result = facade.log(make_event("queue saturation"));

  assert_true(write_result.ok && facade.is_degraded() && facade.fallback_active(),
              "queue saturation should degrade through the fallback path while preserving a successful write result");
  const auto* drop_sample =
      find_sample(meter->samples(), "logging_drop_total", "queue", "degraded");
  assert_true(drop_sample != nullptr,
              "queue saturation advisory should emit logging_drop_total(stage=queue,outcome=degraded)");
  assert_equal(std::string("LOG_E_QUEUE_FULL"),
               drop_sample->labels.error_code,
               "queue saturation drop samples should preserve LOG_E_QUEUE_FULL");
  assert_true(
      find_sample(meter->samples(), "logging_queue_depth", "queue", "degraded") != nullptr,
      "queue saturation should refresh logging_queue_depth(stage=queue,outcome=degraded)");
  assert_equal(0,
               static_cast<int>(count_samples_named(meter->samples(),
                                                    "logging_write_total")),
               "queue saturation advisory should not misclassify the dropped record as an accepted write_total sample");
}

void test_logging_metrics_bridge_main_chain_emits_failure_samples_when_recovery_fails() {
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::LoggingHealthSampleState;
  using dasall::infra::logging::LoggingMetricsBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto meter = std::make_shared<RecordingMeter>();
  auto provider = std::make_shared<RecordingMetricsProvider>(meter);

  LoggingFacade facade(std::make_unique<ScriptedDispatchBackend>(),
                       std::make_shared<FailingRecoverySink>());
  assert_true(facade.init(make_context()).ok,
              "recovery failure metrics test should initialize the facade first");
  facade.attach_metrics_bridge(
      std::make_shared<LoggingMetricsBridge>(provider, "desktop_full"),
      1U);
  facade.set_force_format_failure_for_tests(true);

  const auto write_result = facade.log(make_event("format failure before fallback"));
  const auto health_sample = facade.sample(100);

  assert_true(!write_result.ok,
              "failed recovery should surface a failed log result once fallback persistence is unavailable");
  assert_true(
      find_sample(meter->samples(), "logging_write_fail_total", "write", "failure") != nullptr,
      "formatter failure should emit logging_write_fail_total(stage=write,outcome=failure)");
  assert_true(find_sample(meter->samples(),
                          "logging_write_fail_total",
                          "recovery",
                          "failure") != nullptr,
              "recovery fallback failure should emit logging_write_fail_total(stage=recovery,outcome=failure)");
  assert_true(
      find_sample(meter->samples(), "logging_queue_depth", "queue", "failure") != nullptr,
      "failed recovery should still refresh logging_queue_depth(stage=queue,outcome=failure)");
  assert_true(health_sample.state == LoggingHealthSampleState::Ready,
              "main-chain metrics failure should still leave the logging health provider sampleable");
  assert_equal(1,
               static_cast<int>(health_sample.signals.unrecoverable_failure_total),
               "failed recovery should raise unrecoverable_failure_total for the logging health probe");
}

}  // namespace

int main() {
  try {
    test_logging_metrics_bridge_main_chain_emits_accepted_and_flush_samples();
    test_logging_metrics_bridge_main_chain_emits_drop_samples_for_queue_saturation();
    test_logging_metrics_bridge_main_chain_emits_failure_samples_when_recovery_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
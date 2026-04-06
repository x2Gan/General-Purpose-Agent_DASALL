#include "metrics/MetricReaderScheduler.h"

#include <string>

#include "metrics/MetricsErrors.h"

namespace dasall::infra::metrics {
namespace {

constexpr std::string_view kMetricReaderSchedulerSourceRef = "MetricReaderScheduler";

[[nodiscard]] MetricsOperationStatus make_scheduler_failure(MetricsErrorCode code,
                                                            std::string message,
                                                            std::string stage) {
  const auto mapping = map_metrics_error_code(code);
  return MetricsOperationStatus::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kMetricReaderSchedulerSourceRef) + ":" +
          std::string(metrics_error_code_name(code)));
}

}  // namespace

MetricReaderScheduler::MetricReaderScheduler()
    : MetricReaderScheduler(MetricsConfigPolicy{}.default_config()) {}

MetricReaderScheduler::MetricReaderScheduler(MetricsResolvedConfig config)
    : config_(config.is_valid() ? std::move(config) : MetricsConfigPolicy{}.default_config()) {}

MetricReaderTickResult MetricReaderScheduler::schedule_tick(
    std::int64_t now_unix_ms,
    const AggregationSnapshot& snapshot) {
  if (now_unix_ms <= 0) {
    return invalid_request("metric reader scheduler requires a positive wall-clock timestamp",
                           "metrics.reader.schedule_tick");
  }

  if (!config_.enabled || snapshot.empty()) {
    return MetricReaderTickResult{
        .triggered = false,
        .shutdown_flush = false,
        .batch = {},
        .status = MetricsOperationStatus::success("metrics-reader://idle"),
    };
  }

  if (last_tick_unix_ms_ == 0) {
    if (static_cast<std::uint64_t>(now_unix_ms) < config_.reader_interval_ms) {
      return MetricReaderTickResult{
          .triggered = false,
          .shutdown_flush = false,
          .batch = {},
          .status = MetricsOperationStatus::success("metrics-reader://waiting-first-interval"),
      };
    }
  } else if (static_cast<std::uint64_t>(now_unix_ms - last_tick_unix_ms_) <
             config_.reader_interval_ms) {
    return MetricReaderTickResult{
        .triggered = false,
        .shutdown_flush = false,
        .batch = {},
        .status = MetricsOperationStatus::success("metrics-reader://waiting-next-interval"),
    };
  }

  return enqueue_batch(now_unix_ms, snapshot, false, "tick");
}

MetricReaderTickResult MetricReaderScheduler::flush_on_shutdown(
    std::int64_t now_unix_ms,
    const AggregationSnapshot& snapshot) {
  if (now_unix_ms <= 0) {
    return invalid_request("metric reader scheduler requires a positive shutdown timestamp",
                           "metrics.reader.flush_on_shutdown");
  }

  if (!config_.enabled || snapshot.empty()) {
    return MetricReaderTickResult{
        .triggered = false,
        .shutdown_flush = false,
        .batch = {},
        .status = MetricsOperationStatus::success("metrics-reader://shutdown-noop"),
    };
  }

  return enqueue_batch(now_unix_ms, snapshot, true, "shutdown");
}

std::optional<MetricExportBatch> MetricReaderScheduler::pop_next_batch() {
  if (pending_batches_.empty()) {
    return std::nullopt;
  }

  auto batch = pending_batches_.front();
  pending_batches_.pop_front();
  return batch;
}

std::size_t MetricReaderScheduler::pending_batch_count() const {
  return pending_batches_.size();
}

const std::optional<MetricExportBatch>& MetricReaderScheduler::last_batch() const {
  return last_batch_;
}

std::int64_t MetricReaderScheduler::last_tick_unix_ms() const {
  return last_tick_unix_ms_;
}

const MetricsResolvedConfig& MetricReaderScheduler::config() const {
  return config_;
}

MetricReaderTickResult MetricReaderScheduler::enqueue_batch(
    std::int64_t now_unix_ms,
    const AggregationSnapshot& snapshot,
    bool shutdown_flush,
    const char* batch_reason) {
  const MetricExportBatch batch{
      .batch_id = std::string("metrics-batch://") + batch_reason + "/" +
                  std::to_string(++next_batch_sequence_),
      .sample_count = count_samples(snapshot),
      .exporter_type = config_.exporter_type,
  };

  if (!batch.is_valid()) {
    return invalid_request(
        "metric reader scheduler requires a non-empty snapshot before queuing an export batch",
        shutdown_flush ? "metrics.reader.flush_on_shutdown"
                       : "metrics.reader.schedule_tick");
  }

  pending_batches_.push_back(batch);
  last_batch_ = batch;
  last_tick_unix_ms_ = now_unix_ms;
  return MetricReaderTickResult{
      .triggered = true,
      .shutdown_flush = shutdown_flush,
      .batch = batch,
      .status = MetricsOperationStatus::success(shutdown_flush
                                                    ? "metrics-reader://shutdown-flushed"
                                                    : "metrics-reader://scheduled"),
  };
}

MetricReaderTickResult MetricReaderScheduler::invalid_request(std::string message,
                                                              std::string stage) const {
  return MetricReaderTickResult{
      .triggered = false,
      .shutdown_flush = false,
      .batch = {},
      .status = make_scheduler_failure(MetricsErrorCode::ConfigInvalid,
                                       std::move(message),
                                       std::move(stage)),
  };
}

std::size_t MetricReaderScheduler::count_samples(const AggregationSnapshot& snapshot) {
  std::size_t total_samples = 0;
  for (const auto& [metric_name, aggregate] : snapshot.metrics) {
    (void)metric_name;
    total_samples += static_cast<std::size_t>(aggregate.sample_count);
  }

  return total_samples;
}

}  // namespace dasall::infra::metrics
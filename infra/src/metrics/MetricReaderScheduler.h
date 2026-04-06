#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

#include "metrics/AggregationEngine.h"
#include "metrics/IMetricExporter.h"
#include "metrics/MetricsConfigPolicy.h"

namespace dasall::infra::metrics {

struct MetricReaderTickResult {
  bool triggered = false;
  bool shutdown_flush = false;
  MetricExportBatch batch{};
  MetricsOperationStatus status = MetricsOperationStatus::success();
};

class MetricReaderScheduler {
 public:
  MetricReaderScheduler();
  explicit MetricReaderScheduler(MetricsResolvedConfig config);

  [[nodiscard]] MetricReaderTickResult schedule_tick(std::int64_t now_unix_ms,
                                                     const AggregationSnapshot& snapshot);
  [[nodiscard]] MetricReaderTickResult flush_on_shutdown(
      std::int64_t now_unix_ms,
      const AggregationSnapshot& snapshot);
  [[nodiscard]] std::optional<MetricExportBatch> pop_next_batch();
  [[nodiscard]] std::size_t pending_batch_count() const;
  [[nodiscard]] const std::optional<MetricExportBatch>& last_batch() const;
  [[nodiscard]] std::int64_t last_tick_unix_ms() const;
  [[nodiscard]] const MetricsResolvedConfig& config() const;

 private:
  [[nodiscard]] MetricReaderTickResult enqueue_batch(std::int64_t now_unix_ms,
                                                     const AggregationSnapshot& snapshot,
                                                     bool shutdown_flush,
                                                     const char* batch_reason);
  [[nodiscard]] MetricReaderTickResult invalid_request(std::string message,
                                                       std::string stage) const;
  [[nodiscard]] static std::size_t count_samples(const AggregationSnapshot& snapshot);

  MetricsResolvedConfig config_;
  std::deque<MetricExportBatch> pending_batches_;
  std::optional<MetricExportBatch> last_batch_;
  std::uint64_t next_batch_sequence_ = 0;
  std::int64_t last_tick_unix_ms_ = 0;
};

}  // namespace dasall::infra::metrics
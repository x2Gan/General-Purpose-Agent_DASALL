#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "tracing/SpanImpl.h"
#include "tracing/TraceConfig.h"
#include "tracing/TraceErrors.h"

namespace dasall::infra::tracing {

enum class BatchSpanBufferTrigger {
  None = 0,
  QueueThreshold,
  ScheduleDelay,
};

struct BatchSpanEnqueueResult {
  TraceOperationStatus status = TraceOperationStatus::success("trace-buffer://idle");
  bool accepted = false;
  bool would_block = false;
  bool dropped_oldest = false;
  bool export_requested = false;
  std::uint32_t queue_depth = 0;
  std::uint64_t dropped_total = 0;

  [[nodiscard]] bool has_consistent_values() const {
    if (would_block && (accepted || dropped_oldest || export_requested)) {
      return false;
    }

    return status.references_only_contract_error_types();
  }
};

class BatchSpanBuffer {
 public:
  explicit BatchSpanBuffer(TraceConfig config = {});

  [[nodiscard]] BatchSpanEnqueueResult enqueue(const std::shared_ptr<SpanImpl>& span);
  [[nodiscard]] std::vector<std::shared_ptr<SpanImpl>> dequeue_batch(
      std::size_t limit = 0U);
  [[nodiscard]] std::vector<std::shared_ptr<SpanImpl>> force_flush();
  [[nodiscard]] BatchSpanBufferTrigger export_trigger(std::int64_t now_unix_ms) const;
  [[nodiscard]] bool should_export_now(std::int64_t now_unix_ms) const;
  void mark_export_cycle_complete(std::int64_t completed_at_unix_ms);

  [[nodiscard]] const BatchSpanEnqueueResult& last_enqueue_result() const;
  [[nodiscard]] std::size_t queue_depth() const;
  [[nodiscard]] std::uint64_t dropped_total() const;
  [[nodiscard]] std::uint64_t blocked_enqueue_total() const;
  [[nodiscard]] const std::optional<std::int64_t>& oldest_pending_end_ts() const;
  [[nodiscard]] const std::optional<std::int64_t>& last_export_completed_ts() const;

 private:
  using SpanQueue = std::deque<std::shared_ptr<SpanImpl>>;

  [[nodiscard]] static TraceOperationStatus make_failure(TraceErrorCode code,
                                                         std::string message,
                                                         std::string stage);
  [[nodiscard]] static TraceOperationStatus make_validation_failure(std::string message,
                                                                    std::string stage);
  void refresh_oldest_pending_end_ts();
  [[nodiscard]] std::size_t default_batch_limit() const;

  TraceConfig config_;
  SpanQueue queue_;
  BatchSpanEnqueueResult last_enqueue_result_{};
  std::uint64_t dropped_total_ = 0;
  std::uint64_t blocked_enqueue_total_ = 0;
  std::optional<std::int64_t> oldest_pending_end_ts_;
  std::optional<std::int64_t> last_export_completed_ts_;
};

}  // namespace dasall::infra::tracing
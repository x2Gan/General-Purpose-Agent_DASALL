#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "tracing/BatchSpanBuffer.h"
#include "tracing/TraceErrors.h"
#include "tracing/TracerImpl.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::tracing::TracerScope make_scope() {
  return dasall::infra::tracing::TracerScope{
      .name = std::string("infra.tracing"),
      .version = std::string("1.0.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };
}

[[nodiscard]] dasall::infra::tracing::SpanDescriptor make_descriptor(std::string name) {
  return dasall::infra::tracing::SpanDescriptor{
      .name = std::move(name),
      .kind = dasall::infra::tracing::SpanKind::Internal,
      .start_ts_unix_ms = 1712534400000,
      .attrs = {{"component", dasall::infra::tracing::TraceAttributeValue{std::string("tracing")}}},
      .links = {},
  };
}

[[nodiscard]] std::shared_ptr<dasall::infra::tracing::SpanImpl> make_ended_span(
    std::string name,
    std::int64_t end_ts_unix_ms) {
  dasall::infra::tracing::TraceConfig config;
  config.sampler.type = std::string("always_on");
  dasall::infra::tracing::TracerImpl tracer(make_scope(), config);

  const auto span_base = tracer.start_span(make_descriptor(std::move(name)), nullptr);
  const auto span = std::dynamic_pointer_cast<dasall::infra::tracing::SpanImpl>(span_base);
  const auto end_result = span->end(end_ts_unix_ms);
  (void)end_result;
  return span;
}

[[nodiscard]] dasall::infra::tracing::TraceConfig make_buffer_config(
    std::string overflow_policy,
    std::uint32_t max_queue_size,
    std::uint32_t max_export_batch_size,
    std::uint32_t schedule_delay_ms) {
  dasall::infra::tracing::TraceConfig config;
  config.batch.max_queue_size = max_queue_size;
  config.batch.max_export_batch_size = max_export_batch_size;
  config.batch.schedule_delay_ms = schedule_delay_ms;
  config.overflow_policy = std::move(overflow_policy);
  return config;
}

void test_batch_span_buffer_triggers_export_on_batch_threshold_and_preserves_fifo() {
  using dasall::infra::tracing::BatchSpanBuffer;
  using dasall::infra::tracing::BatchSpanBufferTrigger;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  BatchSpanBuffer buffer(make_buffer_config("drop_oldest", 4U, 2U, 5000U));

  const auto first = buffer.enqueue(make_ended_span("runtime.first", 1000));
  const auto second = buffer.enqueue(make_ended_span("runtime.second", 1001));

  assert_true(first.accepted && !first.export_requested && second.accepted &&
                  second.export_requested && first.has_consistent_values() &&
                  second.has_consistent_values(),
              "BatchSpanBuffer should request export when queue depth reaches max_export_batch_size");
  assert_true(buffer.export_trigger(1001) == BatchSpanBufferTrigger::QueueThreshold,
              "BatchSpanBuffer should surface QueueThreshold when the batch size threshold is met");

  const auto batch = buffer.dequeue_batch();
  assert_equal(static_cast<std::size_t>(2), batch.size(),
               "BatchSpanBuffer should dequeue a full batch when threshold export is requested");
  assert_true(batch.front()->end_result().end_ts_unix_ms == 1000 &&
                  batch.back()->end_result().end_ts_unix_ms == 1001 &&
                  buffer.queue_depth() == 0U,
              "BatchSpanBuffer should preserve FIFO order when dequeuing a batch");
}

void test_batch_span_buffer_blocks_when_capacity_is_exhausted() {
  using dasall::infra::tracing::BatchSpanBuffer;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  BatchSpanBuffer buffer(make_buffer_config("block", 1U, 1U, 5000U));

  assert_true(buffer.enqueue(make_ended_span("runtime.first", 1100)).accepted,
              "BatchSpanBuffer should accept the first ended span under block policy");
  const auto second = buffer.enqueue(make_ended_span("runtime.second", 1101));

  assert_true(!second.accepted && second.would_block && !second.status.ok &&
                  second.status.result_code ==
                      map_trace_error_code(TraceErrorCode::QueueFull).result_code,
              "BatchSpanBuffer should surface TRC_E_QUEUE_FULL when block policy hits capacity");
  assert_equal(1,
               static_cast<int>(buffer.blocked_enqueue_total()),
               "BatchSpanBuffer should increase the blocked counter when block policy overflows");
  assert_equal(1,
               static_cast<int>(buffer.queue_depth()),
               "BatchSpanBuffer should keep queue depth bounded under block overflow");
}

void test_batch_span_buffer_drops_oldest_and_tracks_drop_count() {
  using dasall::infra::tracing::BatchSpanBuffer;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  BatchSpanBuffer buffer(make_buffer_config("drop_oldest", 1U, 1U, 5000U));

  assert_true(buffer.enqueue(make_ended_span("runtime.oldest", 1200)).accepted,
              "BatchSpanBuffer should accept the first span under drop_oldest policy");
  const auto second = buffer.enqueue(make_ended_span("runtime.newest", 1201));

  assert_true(second.accepted && second.dropped_oldest && second.dropped_total == 1U,
              "BatchSpanBuffer should keep the newest span and expose dropped_total under drop_oldest policy");

  const auto drained = buffer.force_flush();
  assert_equal(static_cast<std::size_t>(1), drained.size(),
               "BatchSpanBuffer force_flush should return the remaining newest span");
  assert_true(drained.front()->end_result().end_ts_unix_ms == 1201 && buffer.queue_depth() == 0U,
              "BatchSpanBuffer should retain only the newest span after a drop_oldest replacement");
}

void test_batch_span_buffer_uses_schedule_delay_for_export_trigger() {
  using dasall::infra::tracing::BatchSpanBuffer;
  using dasall::infra::tracing::BatchSpanBufferTrigger;
  using dasall::tests::support::assert_true;

  BatchSpanBuffer buffer(make_buffer_config("drop_oldest", 4U, 4U, 500U));
  assert_true(buffer.enqueue(make_ended_span("runtime.delayed", 1300)).accepted,
              "BatchSpanBuffer should accept a span before schedule-delay evaluation");

  assert_true(!buffer.should_export_now(1799),
              "BatchSpanBuffer should not trigger scheduled export before schedule_delay_ms elapses");
  assert_true(buffer.export_trigger(1800) == BatchSpanBufferTrigger::ScheduleDelay,
              "BatchSpanBuffer should trigger export after schedule_delay_ms relative to the oldest pending span end time");
}

}  // namespace

int main() {
  try {
    test_batch_span_buffer_triggers_export_on_batch_threshold_and_preserves_fifo();
    test_batch_span_buffer_blocks_when_capacity_is_exhausted();
    test_batch_span_buffer_drops_oldest_and_tracks_drop_count();
    test_batch_span_buffer_uses_schedule_delay_for_export_trigger();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
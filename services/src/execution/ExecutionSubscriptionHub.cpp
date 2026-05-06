#include "execution/ExecutionSubscriptionHub.h"

#include <sstream>
#include <utility>

#include "bridges/ServiceMetricsBridge.h"

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::optional<std::uint64_t> parse_cursor(const std::optional<std::string>& cursor) {
  if (!cursor.has_value() || cursor->empty()) {
    return std::uint64_t{0};
  }

  try {
    return static_cast<std::uint64_t>(std::stoull(*cursor));
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::string build_events_json(
    const std::vector<ExecutionSubscriptionEvent>& events) {
  std::ostringstream builder;
  builder << '[';
  for (std::size_t index = 0; index < events.size(); ++index) {
    if (index > 0U) {
      builder << ',';
    }
    builder << events[index].payload_json;
  }
  builder << ']';
  return builder.str();
}

}  // namespace

ExecutionSubscriptionHub::ExecutionSubscriptionHub(ExecutionSubscriptionHubDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

void ExecutionSubscriptionHub::publish(const CapabilityTargetRef& target,
                                       const std::string& stream_kind,
                                       const std::vector<std::string>& events_json_batch) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto& stream = streams_[make_stream_key(target, stream_kind)];

  for (const auto& event_json : events_json_batch) {
    stream.buffer.push_back(ExecutionSubscriptionEvent{
        .sequence = stream.next_sequence++,
        .payload_json = event_json,
    });

    while (stream.buffer.size() > dependencies_.max_buffered_events) {
      stream.buffer.pop_front();
      ++stream.dropped_count;
      stream.resync_required = true;
    }
  }
}

ExecutionSubscriptionResult ExecutionSubscriptionHub::subscribe(const ServiceCallContext&,
                                                                const ExecutionSubscriptionRequest& request) {
  const auto emit_metrics = [&](ExecutionSubscriptionResult result)
      -> ExecutionSubscriptionResult {
    if (dependencies_.metrics_bridge != nullptr) {
      (void)dependencies_.metrics_bridge->record_subscription_result(
          request.target.capability_id,
          request.stream_kind,
          result);
    }

    return result;
  };

  if (request.target.capability_id.empty() || request.target.target_id.empty() ||
      request.stream_kind.empty() || request.max_events == 0U) {
    return emit_metrics(make_validation_failure(
        "capability_id, target_id, stream_kind, and max_events are required",
        "execution_subscription_hub",
        "subscription_request"));
  }

  const auto cursor_sequence = parse_cursor(request.cursor);
  if (!cursor_sequence.has_value()) {
    return emit_metrics(make_validation_failure("cursor must be an unsigned integer string",
                                                "execution_subscription_hub",
                                                "subscription_cursor"));
  }

  const auto stream_key = make_stream_key(request.target, request.stream_kind);
  std::vector<ExecutionSubscriptionEvent> selected_events;
  std::optional<std::string> next_cursor = request.cursor;
  std::uint32_t dropped_count = 0U;
  bool resync_required = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto stream_it = streams_.find(stream_key);
    if (stream_it == streams_.end()) {
      return emit_metrics(ExecutionSubscriptionResult{
          .code = std::nullopt,
          .events_json = "[]",
          .next_cursor = request.cursor,
          .resync_required = false,
          .dropped_count = 0U,
          .error = std::nullopt,
      });
    }

    auto& stream = stream_it->second;
    dropped_count = stream.dropped_count;
    const auto oldest_available_sequence =
        stream.buffer.empty() ? stream.next_sequence : stream.buffer.front().sequence;
    if (*cursor_sequence + 1U < oldest_available_sequence) {
      stream.resync_required = true;
    }

    resync_required = stream.resync_required;

    for (const auto& event : stream.buffer) {
      if (event.sequence <= *cursor_sequence) {
        continue;
      }

      selected_events.push_back(event);
      if (selected_events.size() == request.max_events) {
        break;
      }
    }

    if (!selected_events.empty()) {
      next_cursor = std::to_string(selected_events.back().sequence);
    }
  }

  if (resync_required) {
    return emit_metrics(make_overflow_result(next_cursor,
                                             build_events_json(selected_events),
                                             dropped_count,
                                             stream_key));
  }

  return emit_metrics(ExecutionSubscriptionResult{
      .code = std::nullopt,
      .events_json = build_events_json(selected_events),
      .next_cursor = next_cursor,
      .resync_required = false,
      .dropped_count = dropped_count,
      .error = std::nullopt,
  });
}

std::string ExecutionSubscriptionHub::make_stream_key(const CapabilityTargetRef& target,
                                                      const std::string& stream_kind) const {
  return target.capability_id + ":" + target.target_id + ":" + stream_kind;
}

ExecutionSubscriptionResult ExecutionSubscriptionHub::make_validation_failure(
    const std::string& message,
    const std::string& stage,
    const std::string& ref_id) const {
  return ExecutionSubscriptionResult{
      .code = contracts::ResultCode::ValidationFieldMissing,
      .events_json = {},
      .next_cursor = std::nullopt,
      .resync_required = false,
      .dropped_count = 0U,
      .error = contracts::ErrorInfo{
          .failure_type = contracts::ResultCodeCategory::Validation,
          .retryable = false,
          .safe_to_replan = false,
          .details = {
              .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
              .message = message,
              .stage = stage,
          },
          .source_ref = {
              .ref_type = "services",
              .ref_id = ref_id,
          },
      },
  };
}

ExecutionSubscriptionResult ExecutionSubscriptionHub::make_overflow_result(
    const std::optional<std::string>& next_cursor,
    std::string events_json,
    std::uint32_t dropped_count,
    const std::string& stream_key) const {
  return ExecutionSubscriptionResult{
      .code = contracts::ResultCode::RuntimeRetryExhausted,
      .events_json = std::move(events_json),
      .next_cursor = next_cursor,
      .resync_required = true,
      .dropped_count = dropped_count,
      .error = contracts::ErrorInfo{
          .failure_type = contracts::ResultCodeCategory::Runtime,
          .retryable = true,
          .safe_to_replan = false,
          .details = {
              .code = static_cast<int>(contracts::ResultCode::RuntimeRetryExhausted),
              .message = "subscription overflow requires resync",
              .stage = "execution_subscription_hub",
          },
          .source_ref = {
              .ref_type = "subscription_stream",
              .ref_id = stream_key,
          },
      },
  };
}

}  // namespace dasall::services::internal
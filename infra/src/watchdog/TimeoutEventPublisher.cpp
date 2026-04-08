#include "watchdog/TimeoutEventPublisher.h"

#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kTimeoutEventPublisherSourceRef =
    "TimeoutEventPublisher";

}  // namespace

TimeoutEventPublisher::TimeoutEventPublisher(
    std::shared_ptr<ITimeoutEventSink> event_sink,
    WatchdogServiceConfig config,
    TimeoutEventPublisherOptions options)
    : event_sink_(std::move(event_sink)),
      config_(std::move(config)),
      options_(std::move(options)) {}

void TimeoutEventPublisher::set_event_sink(
    std::shared_ptr<ITimeoutEventSink> event_sink) {
  event_sink_ = std::move(event_sink);
}

TimeoutEventPublishResult TimeoutEventPublisher::publish_timeout(
    const TimeoutDecision& decision) {
  if (!decision.has_required_fields()) {
    return TimeoutEventPublishResult::failure(
        {},
        false,
        fallback_ring_.size(),
        WatchdogErrorCode::EventPublishFail,
        contracts::ResultCode::ValidationFieldMissing,
        "timeout event publisher requires a valid TimeoutDecision before publish_timeout",
        "watchdog.timeout_event.publish",
        std::string(kTimeoutEventPublisherSourceRef));
  }

  if (!should_publish(decision)) {
    record_skip();
    return TimeoutEventPublishResult::skip(fallback_ring_.size());
  }

  TimeoutEvent event = build_event(decision);
  if (!event.has_required_fields()) {
    return TimeoutEventPublishResult::failure(
        std::move(event),
        false,
        fallback_ring_.size(),
        WatchdogErrorCode::EventPublishFail,
        contracts::ResultCode::ValidationFieldMissing,
        "timeout event publisher could not build a valid timeout event payload",
        "watchdog.timeout_event.publish",
        std::string(kTimeoutEventPublisherSourceRef));
  }

  const auto mapping = map_watchdog_error_code(WatchdogErrorCode::EventPublishFail);

  if (!event_sink_) {
    const bool buffered = buffer_event(event);
    record_failure(WatchdogErrorCode::EventPublishFail,
                   mapping.result_code,
                   buffered,
                   false);
    return TimeoutEventPublishResult::failure(
        std::move(event),
        buffered,
        fallback_ring_.size(),
        WatchdogErrorCode::EventPublishFail,
        mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::EventPublishFail)) +
            ": timeout event publisher has no event sink; event buffered locally",
        "watchdog.timeout_event.publish",
        std::string(kTimeoutEventPublisherSourceRef));
  }

  const auto dispatch = event_sink_->publish_timeout_event(event);
  if (dispatch.published) {
    record_success();
    return TimeoutEventPublishResult::success(
        std::move(event),
        dispatch.delivery_ref,
        fallback_ring_.size());
  }

  const bool buffered = buffer_event(event);
  record_failure(WatchdogErrorCode::EventPublishFail,
                 mapping.result_code,
                 buffered,
                 false);
  const std::string message = dispatch.error_info.has_value()
                                  ? dispatch.error_info->details.message
                                  : std::string("timeout event sink rejected the publish request");
  return TimeoutEventPublishResult::failure(
      std::move(event),
      buffered,
      fallback_ring_.size(),
      WatchdogErrorCode::EventPublishFail,
      mapping.result_code,
      std::string(watchdog_error_code_name(WatchdogErrorCode::EventPublishFail)) +
          ": " + message,
      "watchdog.timeout_event.publish",
      std::string(kTimeoutEventPublisherSourceRef));
}

TimeoutEventPublisherStatus TimeoutEventPublisher::status() const {
  return TimeoutEventPublisherStatus{
      .published_total = published_total_,
      .skipped_total = skipped_total_,
      .publish_fail_total = publish_fail_total_,
      .buffered_total = buffered_total_,
      .dropped_total = dropped_total_,
      .degraded = publish_fail_total_ > 0,
      .last_watchdog_code = last_watchdog_code_,
      .last_result_code = last_result_code_,
      .buffered_event_count = fallback_ring_.size(),
  };
}

bool TimeoutEventPublisher::should_publish(const TimeoutDecision& decision) {
  return decision.timeout_level == WatchdogTimeoutLevel::Critical ||
         decision.timeout_level == WatchdogTimeoutLevel::Fatal;
}

TimeoutEvent TimeoutEventPublisher::build_event(
    const TimeoutDecision& decision) const {
  return TimeoutEvent{
      .event_id = std::string("watchdog-timeout://") + decision.entity_id +
                  "/" + std::string(watchdog_timeout_level_name(decision.timeout_level)) +
                  "/" + std::to_string(decision.consecutive_miss),
      .entity_id = decision.entity_id,
      .timeout_level = decision.timeout_level,
      .consecutive_miss = decision.consecutive_miss,
      .reason_code = decision.reason_code,
      .evidence_ref = decision.evidence_ref,
      .trace_id = options_.default_trace_id.empty()
                      ? std::string("unknown")
                      : options_.default_trace_id,
      .session_id = options_.default_session_id.empty()
                        ? std::string("unknown")
                        : options_.default_session_id,
      .task_id = options_.default_task_id.empty()
                     ? std::string("unknown")
                     : options_.default_task_id,
  };
}

bool TimeoutEventPublisher::buffer_event(TimeoutEvent event) {
  if (config_.event_queue_size == 0) {
    return false;
  }

  if (fallback_ring_.size() >= config_.event_queue_size) {
    if (config_.event_overflow_policy ==
        WatchdogEventOverflowPolicy::OverrunOldest) {
      fallback_ring_.pop_front();
      ++dropped_total_;
    } else {
      return false;
    }
  }

  fallback_ring_.push_back(std::move(event));
  return true;
}

void TimeoutEventPublisher::record_success() {
  ++published_total_;
  last_watchdog_code_.reset();
  last_result_code_.reset();
}

void TimeoutEventPublisher::record_skip() {
  ++skipped_total_;
  last_watchdog_code_.reset();
  last_result_code_.reset();
}

void TimeoutEventPublisher::record_failure(WatchdogErrorCode watchdog_code,
                                           contracts::ResultCode result_code,
                                           bool buffered,
                                           bool dropped) {
  ++publish_fail_total_;
  if (buffered) {
    ++buffered_total_;
  }
  if (dropped) {
    ++dropped_total_;
  }

  last_watchdog_code_ = watchdog_code;
  last_result_code_ = result_code;
}

}  // namespace dasall::infra::watchdog
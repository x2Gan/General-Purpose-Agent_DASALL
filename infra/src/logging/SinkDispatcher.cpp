#include "SinkDispatcher.h"

#include <string>
#include <utility>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kSinkDispatcherSourceRef = "SinkDispatcher";

bool has_audit_link_attr(const LogEvent& event) {
  return event.attrs.contains("audit_ref") ||
         event.attrs.contains("audit_ref_pending") ||
         event.attrs.contains("evidence_ref");
}

}  // namespace

SinkDispatcher::SinkDispatcher() : SinkDispatcher(SinkDispatcherOptions{}) {}

SinkDispatcher::SinkDispatcher(AsyncQueueOptions queue_options)
  : SinkDispatcher(SinkDispatcherOptions{
      .queue_options = queue_options,
      .basic_sink = nullptr,
      .audit_sink = nullptr,
    }) {}

SinkDispatcher::SinkDispatcher(SinkDispatcherOptions options)
  : queue_controller_(options.queue_options),
    basic_sink_(std::move(options.basic_sink)),
    audit_sink_(std::move(options.audit_sink)) {}

std::size_t SinkDispatcher::dispatched_record_count(SinkRoute route) const {
  switch (route) {
    case SinkRoute::BasicFile:
      return basic_route_dispatch_count_;
    case SinkRoute::Audit:
      return audit_route_dispatch_count_;
  }

  return 0U;
}

LogWriteResult SinkDispatcher::dispatch(const LogEvent& event) {
  if (!event.attrs_are_serializable()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "sink dispatcher requires serializable log attrs",
        "logging.dispatch",
        std::string(kSinkDispatcherSourceRef));
  }

  const RoutedLogRecord record{
      .route = select_route(event),
      .event = event,
  };

  const auto result = queue_controller_.enqueue(record);
  if (!result.ok) {
    return result;
  }

  if (const auto sink = sink_for_route(record.route); sink != nullptr) {
    const auto sink_result = sink->write(record.event);
    if (!sink_result.ok) {
      return sink_result;
    }
  }

  last_record_ = record;
  ++dispatched_record_count_;
  if (record.route == SinkRoute::Audit) {
    ++audit_route_dispatch_count_;
  } else {
    ++basic_route_dispatch_count_;
  }

  return result;
}

LogWriteResult SinkDispatcher::flush(const LogFlushDeadline& deadline) {
  const auto queue_result = queue_controller_.flush(deadline);
  if (!queue_result.ok) {
    return queue_result;
  }

  if (basic_sink_ != nullptr) {
    const auto basic_result = basic_sink_->flush(deadline);
    if (!basic_result.ok) {
      return basic_result;
    }
  }

  if (audit_sink_ != nullptr && audit_sink_ != basic_sink_) {
    const auto audit_result = audit_sink_->flush(deadline);
    if (!audit_result.ok) {
      return audit_result;
    }
  }

  return LogWriteResult::success();
}

SinkRoute SinkDispatcher::select_route(const LogEvent& event) {
  if (event.category() == "audit" || has_audit_link_attr(event)) {
    return SinkRoute::Audit;
  }

  return SinkRoute::BasicFile;
}

std::shared_ptr<ILogSink> SinkDispatcher::sink_for_route(SinkRoute route) const {
  if (route == SinkRoute::Audit) {
    return audit_sink_ != nullptr ? audit_sink_ : basic_sink_;
  }

  return basic_sink_;
}

}  // namespace dasall::infra::logging
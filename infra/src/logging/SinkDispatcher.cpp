#include "SinkDispatcher.h"

#include <string>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kSinkDispatcherSourceRef = "SinkDispatcher";

bool has_audit_link_attr(const LogEvent& event) {
  return event.attrs.contains("audit_ref") ||
         event.attrs.contains("audit_ref_pending") ||
         event.attrs.contains("evidence_ref");
}

}  // namespace

SinkDispatcher::SinkDispatcher() = default;

SinkDispatcher::SinkDispatcher(AsyncQueueOptions queue_options)
    : queue_controller_(queue_options) {}

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
  return queue_controller_.flush(deadline);
}

SinkRoute SinkDispatcher::select_route(const LogEvent& event) {
  if (event.category() == "audit" || has_audit_link_attr(event)) {
    return SinkRoute::Audit;
  }

  return SinkRoute::BasicFile;
}

}  // namespace dasall::infra::logging
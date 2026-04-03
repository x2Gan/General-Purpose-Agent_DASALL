#include "SinkDispatcher.h"

#include <algorithm>
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

std::string_view sink_route_name(SinkRoute route) {
  switch (route) {
    case SinkRoute::BasicFile:
      return "basic_file";
    case SinkRoute::Audit:
      return "audit";
  }

  return "unknown";
}

LogWriteResult SinkDispatcher::dispatch(const LogEvent& event) {
  if (!event.attrs_are_serializable()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "sink dispatcher requires serializable log attrs",
        "logging.dispatch",
        std::string(kSinkDispatcherSourceRef));
  }

  records_.push_back(RoutedLogRecord{
      .route = select_route(event),
      .event = event,
  });
  return LogWriteResult::success();
}

LogWriteResult SinkDispatcher::flush(const LogFlushDeadline& deadline) {
  if (!deadline.is_valid()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "sink dispatcher flush deadline must be greater than zero",
        "logging.flush",
        std::string(kSinkDispatcherSourceRef));
  }

  last_flush_timeout_ms_ = deadline.timeout_ms;
  return LogWriteResult::success();
}

std::size_t SinkDispatcher::dispatched_record_count(SinkRoute route) const {
  return static_cast<std::size_t>(std::count_if(
      records_.begin(),
      records_.end(),
      [route](const RoutedLogRecord& record) {
        return record.route == route;
      }));
}

SinkRoute SinkDispatcher::select_route(const LogEvent& event) {
  if (event.category() == "audit" || has_audit_link_attr(event)) {
    return SinkRoute::Audit;
  }

  return SinkRoute::BasicFile;
}

}  // namespace dasall::infra::logging
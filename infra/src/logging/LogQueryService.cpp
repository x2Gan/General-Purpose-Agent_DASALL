#include "LogQueryService.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kLogQueryServiceSourceRef = "LogQueryService";

}  // namespace

LogQueryResult LogQueryResult::success(std::string artifact_ref,
                                       std::uint32_t match_count,
                                       bool truncated,
                                       std::string checksum,
                                       std::int64_t created_at) {
  return LogQueryResult{
      .ok = true,
      .artifact_ref = std::move(artifact_ref),
      .match_count = match_count,
      .truncated = truncated,
      .checksum = std::move(checksum),
      .created_at = created_at,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .error_info = std::nullopt,
  };
}

LogQueryResult LogQueryResult::failure(contracts::ResultCode result_code,
                                       std::string message,
                                       std::string stage,
                                       std::string source_ref) {
  return LogQueryResult{
      .ok = false,
      .artifact_ref = {},
      .match_count = 0,
      .truncated = false,
      .checksum = {},
      .created_at = 0,
      .result_code = result_code,
      .error_info = contracts::ErrorInfo{
          .failure_type = contracts::classify_result_code(result_code),
          .retryable = false,
          .safe_to_replan = false,
          .details = contracts::ErrorDetails{
              .code = static_cast<int>(result_code),
              .message = std::move(message),
              .stage = std::move(stage),
          },
          .source_ref = contracts::ErrorSourceRefMinimal{
              .ref_type = "infra.logging",
              .ref_id = std::move(source_ref),
          },
      },
  };
}

LogQueryService::LogQueryService(std::shared_ptr<ILogQueryRecordReader> record_reader,
                                 LogQueryServiceOptions options,
                                 ClockNowMs clock_now_ms)
    : record_reader_(std::move(record_reader)),
      options_(std::move(options)),
      clock_now_ms_(std::move(clock_now_ms)) {
  if (!clock_now_ms_) {
    clock_now_ms_ = &LogQueryService::default_now_ms;
  }
}

LogQueryResult LogQueryService::query(const LogQueryRequest& request,
                                      const LogQueryAccessContext& access_context) const {
  if (!options_.has_consistent_values()) {
    return LogQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log query service requires a non-empty local artifact namespace",
        "logging.query.config",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!request.has_required_fields()) {
    return LogQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log query request must keep query_id, precise selector, ordered time window, and positive max_records",
        "logging.query.request",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!access_context.has_required_fields()) {
    return LogQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log query access context must keep actor_ref, consumer_module, and a complete policy decision reference",
        "logging.query.access",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!options_.enable_diag_pull) {
    return LogQueryResult::failure(
        contracts::ResultCode::PolicyDenied,
        "infra.logging.export.enable_diag_pull must remain enabled before log query export is allowed",
        "logging.query.config_gate",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!access_context.has_allow_proof()) {
    return LogQueryResult::failure(
        contracts::ResultCode::PolicyDenied,
        "log query requires an upstream allow decision proof and does not re-authorize denied or confirmation-required requests",
        "logging.query.policy",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!record_reader_) {
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query requires a local record reader before it can materialize a diagnostics artifact",
        "logging.query.reader",
        std::string(kLogQueryServiceSourceRef));
  }

  const auto records = record_reader_->read_window(request.start_ts_ms, request.end_ts_ms);
  std::vector<LogEvent> matches;
  matches.reserve(records.size());
  for (const auto& record : records) {
    if (matches_selector(record, request)) {
      matches.push_back(record);
    }
  }

  const bool truncated = matches.size() > request.max_records;
  const auto returned_match_count = static_cast<std::uint32_t>(
      truncated ? request.max_records : matches.size());

  const auto created_at = clock_now_ms_();
  if (created_at <= 0) {
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query artifact generation requires a positive created_at timestamp",
        "logging.query.artifact",
        std::string(kLogQueryServiceSourceRef));
  }

  return LogQueryResult::success(make_artifact_ref(request),
                                 returned_match_count,
                                 truncated,
                                 make_checksum(request, returned_match_count, truncated),
                                 created_at);
}

std::string_view LogQueryService::selector_attr_key(
    LogQuerySelectorKind selector_kind) {
  switch (selector_kind) {
    case LogQuerySelectorKind::TraceId:
      return "trace_id";
    case LogQuerySelectorKind::SessionId:
      return "session_id";
    case LogQuerySelectorKind::Unspecified:
      break;
  }

  return "unspecified";
}

bool LogQueryService::matches_selector(const LogEvent& event,
                                       const LogQueryRequest& request) {
  if (!event.has_timestamp()) {
    return false;
  }

  if (*event.ts < request.start_ts_ms || *event.ts > request.end_ts_ms) {
    return false;
  }

  const auto iterator = event.attrs.find(std::string(selector_attr_key(request.selector_kind)));
  return iterator != event.attrs.end() && iterator->second == request.selector_value;
}

std::string LogQueryService::make_artifact_ref(const LogQueryRequest& request) const {
  return options_.artifact_namespace + "/" + request.query_id;
}

std::string LogQueryService::make_checksum(const LogQueryRequest& request,
                                          std::uint32_t match_count,
                                          bool truncated) {
  return std::string("log-query:") + request.query_id + ":" +
         std::string(log_query_selector_name(request.selector_kind)) + ":" +
         request.selector_value + ":" + std::to_string(match_count) + ":" +
         (truncated ? "truncated" : "complete");
}

std::int64_t LogQueryService::default_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace dasall::infra::logging
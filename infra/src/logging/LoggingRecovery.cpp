#include "LoggingRecovery.h"

#include <string>
#include <string_view>
#include <utility>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kLoggingRecoverySourceRef = "LoggingRecovery";

}  // namespace

LoggingRecovery::LoggingRecovery(std::shared_ptr<ILogRecoverySink> primary_sink,
                                 std::shared_ptr<ILogRecoverySink> fallback_sink)
    : primary_sink_(std::move(primary_sink)),
      fallback_sink_(std::move(fallback_sink)) {}

LoggingRecoveryResult LoggingRecovery::write(const LogEvent& event) {
  if (!primary_sink_ || !fallback_sink_) {
    return make_failure_result(
        LoggingErrorCode::ConfigInvalid,
        "logging recovery requires both primary and fallback sink injection points",
        "logging.recovery.config",
        false,
        degraded_,
        false);
  }

  if (degraded_) {
    return write_to_fallback(
        event,
        last_error_code_.value_or(LoggingErrorCode::SinkIo),
        "logging recovery remains degraded and routes writes to the fallback sink",
        false);
  }

  const auto primary_result = primary_sink_->write(event);
  if (primary_result.ok) {
    return LoggingRecoveryResult{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .degraded = false,
        .recovery_attempted = false,
        .recovery_succeeded = false,
        .logging_error_code = std::nullopt,
        .result_code = std::nullopt,
    };
  }

  return write_to_fallback(event,
                           LoggingErrorCode::SinkIo,
                           "primary sink write failed",
                           false);
}

LoggingRecoveryResult LoggingRecovery::handle_format_failure(const LogEvent& event) {
  if (!fallback_sink_) {
    return make_failure_result(
        LoggingErrorCode::ConfigInvalid,
        "logging recovery requires a fallback sink before handling format failures",
        "logging.recovery.format",
        false,
        degraded_,
        false);
  }

  return write_to_fallback(make_minimal_fallback_event(event),
                           LoggingErrorCode::FormatInvalid,
                           "structured formatter failed and downgraded to the minimal fallback record",
                           false);
}

LoggingRecoveryResult LoggingRecovery::retry_primary_sink(const LogEvent& probe_event) {
  if (!primary_sink_ || !fallback_sink_) {
    return make_failure_result(
        LoggingErrorCode::ConfigInvalid,
        "logging recovery requires both primary and fallback sink injection points before retry",
        "logging.recovery.retry",
        false,
        degraded_,
        true);
  }

  if (!degraded_) {
    return LoggingRecoveryResult{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .degraded = false,
        .recovery_attempted = false,
        .recovery_succeeded = false,
        .logging_error_code = std::nullopt,
        .result_code = std::nullopt,
    };
  }

  ++retry_attempt_total_;

  const auto primary_result = primary_sink_->write(probe_event);
  if (primary_result.ok) {
    degraded_ = false;
    fallback_active_ = false;
    ++recovery_success_total_;
    last_failure_reason_.clear();
    last_error_code_.reset();
    last_fallback_event_.reset();
    return LoggingRecoveryResult{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .degraded = false,
        .recovery_attempted = true,
        .recovery_succeeded = true,
        .logging_error_code = std::nullopt,
        .result_code = std::nullopt,
    };
  }

  ++recovery_failure_total_;
  return write_to_fallback(probe_event,
                           LoggingErrorCode::SinkIo,
                           "primary sink retry failed",
                           true);
}

LoggingRecoveryResult LoggingRecovery::write_to_fallback(
    const LogEvent& event,
    LoggingErrorCode error_code,
    std::string_view reason,
    bool recovery_attempted) {
  degraded_ = true;
  fallback_active_ = true;
  last_failure_reason_ = std::string(reason);
  last_error_code_ = error_code;
  last_fallback_event_ = event;

  const auto fallback_result = fallback_sink_->write(event);
  if (fallback_result.ok) {
    return LoggingRecoveryResult{
        .accepted = true,
        .persisted = true,
        .fallback_used = true,
        .degraded = true,
        .recovery_attempted = recovery_attempted,
        .recovery_succeeded = false,
        .logging_error_code = error_code,
        .result_code = std::nullopt,
    };
  }

  return make_failure_result(
      error_code,
      "fallback sink write failed while the logging recovery path was active",
      "logging.recovery.fallback",
      true,
      true,
      recovery_attempted);
}

LoggingRecoveryResult LoggingRecovery::make_failure_result(
    LoggingErrorCode error_code,
    std::string message,
    std::string stage,
    bool fallback_used,
    bool degraded,
    bool recovery_attempted) {
  const auto mapping = map_logging_error_code(error_code);
  const auto failure = LogWriteResult::failure(mapping.result_code,
                                               std::move(message),
                                               std::move(stage),
                                               std::string(kLoggingRecoverySourceRef));

  return LoggingRecoveryResult{
      .accepted = false,
      .persisted = false,
      .fallback_used = fallback_used,
      .degraded = degraded,
      .recovery_attempted = recovery_attempted,
      .recovery_succeeded = false,
      .logging_error_code = error_code,
      .result_code = failure.result_code,
  };
}

LogEvent LoggingRecovery::make_minimal_fallback_event(const LogEvent& event) {
  auto fallback_event = event;
  fallback_event.attrs.clear();

  if (fallback_event.module.empty()) {
    fallback_event.module = std::string("logging");
  }

  return fallback_event;
}

}  // namespace dasall::infra::logging
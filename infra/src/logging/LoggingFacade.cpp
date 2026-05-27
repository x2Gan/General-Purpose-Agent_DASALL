#include "LoggingFacade.h"

#include "SinkDispatcher.h"

#include <memory>
#include <string>
#include <utility>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kLoggingFacadeSourceRef = "LoggingFacade";

std::unique_ptr<ILogDispatchBackend> make_default_dispatch_backend() {
  return std::make_unique<SinkDispatcher>();
}

std::string normalize_identifier(std::string value) {
  if (value.empty()) {
    return std::string(LogContext::kUnknownIdentifier);
  }

  return value;
}

}  // namespace

LoggingFacade::LoggingFacade()
    : dispatch_backend_(make_default_dispatch_backend()) {}

LoggingFacade::LoggingFacade(std::unique_ptr<ILogDispatchBackend> dispatch_backend)
    : dispatch_backend_(std::move(dispatch_backend)) {
  if (!dispatch_backend_) {
    dispatch_backend_ = make_default_dispatch_backend();
  }
}

InfraOperationResult LoggingFacade::init(const LogContext& context) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  current_context_ = normalize_context(context);
  current_level_ = LogLevel::Info;
  last_dispatched_event_.reset();
  dispatched_record_count_ = 0;

  if (!dispatch_backend_) {
    dispatch_backend_ = make_default_dispatch_backend();
  }

  lifecycle_state_ = LifecycleState::Initialized;
  return InfraOperationResult::success();
}

InfraOperationResult LoggingFacade::stop() {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return invalid_transition("stop", "initialized");
  }

  lifecycle_state_ = LifecycleState::Stopped;
  return InfraOperationResult::success();
}

LogWriteResult LoggingFacade::log(const LogEvent& event) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "logging facade must be initialized before log()",
        "logging.lifecycle",
        std::string(kLoggingFacadeSourceRef));
  }

  if (!event.attrs_are_serializable()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log event attrs must remain serializable",
        "logging.log",
        std::string(kLoggingFacadeSourceRef));
  }

  if (!is_enabled_for_level(event.level, current_level_)) {
    return LogWriteResult::success();
  }

  auto enriched_event = enrich_event(event);
  auto redacted_event = redaction_filter_.apply(enriched_event);
  auto formatted_event = structured_formatter_.format(redacted_event);
  const auto result = dispatch_backend_->dispatch(formatted_event);
  if (result.ok) {
    last_dispatched_event_ = std::move(formatted_event);
    ++dispatched_record_count_;
  }

  return result;
}

LogWriteResult LoggingFacade::flush(const LogFlushDeadline& deadline) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "logging facade must be initialized before flush()",
        "logging.lifecycle",
        std::string(kLoggingFacadeSourceRef));
  }

  if (!deadline.is_valid()) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "flush deadline must be greater than zero",
        "logging.flush",
        std::string(kLoggingFacadeSourceRef));
  }

  return dispatch_backend_->flush(deadline);
}

void LoggingFacade::set_level(LogLevel level) {
  current_level_ = level;
}

void LoggingFacade::set_dispatch_backend(
    std::unique_ptr<ILogDispatchBackend> dispatch_backend) {
  dispatch_backend_ = dispatch_backend != nullptr
                          ? std::move(dispatch_backend)
                          : make_default_dispatch_backend();
}

std::string_view LoggingFacade::lifecycle_state_name() const {
  switch (lifecycle_state_) {
    case LifecycleState::Created:
      return "created";
    case LifecycleState::Initialized:
      return "initialized";
    case LifecycleState::Stopped:
      return "stopped";
  }

  return "unknown";
}

InfraOperationResult LoggingFacade::invalid_transition(
    std::string_view operation,
    std::string_view expected_state) const {
  return InfraOperationResult::failure(
      contracts::ResultCode::RuntimeRetryExhausted,
      "invalid logging lifecycle transition for operation " +
          std::string(operation) + ": expected state " +
          std::string(expected_state) + ", actual state " +
          std::string(lifecycle_state_name()),
      "logging.lifecycle",
      std::string(kLoggingFacadeSourceRef));
}

LogContext LoggingFacade::normalize_context(LogContext context) {
  context.request_id = normalize_identifier(std::move(context.request_id));
  context.session_id = normalize_identifier(std::move(context.session_id));
  context.trace_id = normalize_identifier(std::move(context.trace_id));
  context.task_id = normalize_identifier(std::move(context.task_id));
  context.parent_task_id = normalize_identifier(std::move(context.parent_task_id));
  context.lease_id = normalize_identifier(std::move(context.lease_id));
  return context;
}

bool LoggingFacade::is_enabled_for_level(LogLevel event_level,
                                         LogLevel current_level) {
  if (event_level == LogLevel::Unspecified ||
      current_level == LogLevel::Unspecified) {
    return true;
  }

  return static_cast<int>(event_level) >= static_cast<int>(current_level);
}

LogEvent LoggingFacade::enrich_event(const LogEvent& event) const {
  auto enriched = event;
  enriched.attrs.try_emplace("request_id", current_context_.request_id);
  enriched.attrs.try_emplace("session_id", current_context_.session_id);
  enriched.attrs.try_emplace("trace_id", current_context_.trace_id);
  enriched.attrs.try_emplace("task_id", current_context_.task_id);
  enriched.attrs.try_emplace("parent_task_id", current_context_.parent_task_id);
  enriched.attrs.try_emplace("lease_id", current_context_.lease_id);
  return enriched;
}

}  // namespace dasall::infra::logging
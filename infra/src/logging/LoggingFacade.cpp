#include "LoggingFacade.h"

#include "SinkDispatcher.h"

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kLoggingFacadeSourceRef = "LoggingFacade";
constexpr std::uint32_t kLoggingFacadeStopFlushTimeoutMs = 500;
constexpr std::uint32_t kLoggingRecoveryProbeFlushTimeoutMs = 500;
constexpr std::size_t kFallbackRingBufferCapacity = 64U;

std::unique_ptr<ILogDispatchBackend> make_default_dispatch_backend() {
  return std::make_unique<SinkDispatcher>();
}

class DispatchBackendRecoverySink final : public ILogRecoverySink {
 public:
  explicit DispatchBackendRecoverySink(ILogDispatchBackend* dispatch_backend)
      : dispatch_backend_(dispatch_backend) {}

  LogWriteResult write(const LogEvent& event) override {
    if (dispatch_backend_ == nullptr) {
      return LogWriteResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          "logging recovery requires a concrete primary dispatch backend",
          "logging.recovery.primary",
          std::string(kLoggingFacadeSourceRef));
    }

    const auto dispatch_result = dispatch_backend_->dispatch(event);
    if (!dispatch_result.ok) {
      return dispatch_result;
    }

    return dispatch_backend_->flush(
        LogFlushDeadline{.timeout_ms = kLoggingRecoveryProbeFlushTimeoutMs});
  }

 private:
  ILogDispatchBackend* dispatch_backend_ = nullptr;
};

class RingBufferFallbackSink final : public ILogRecoverySink {
 public:
  LogWriteResult write(const LogEvent& event) override {
    if (!event.attrs_are_serializable()) {
      return LogWriteResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          "fallback sink requires serializable log attrs",
          "logging.recovery.fallback",
          std::string(kLoggingFacadeSourceRef));
    }

    if (buffer_.size() == kFallbackRingBufferCapacity) {
      buffer_.pop_front();
    }
    buffer_.push_back(event);

    if (!event.message.empty()) {
      std::cerr << event.message << std::endl;
    }

    return LogWriteResult::success();
  }

 private:
  std::deque<LogEvent> buffer_;
};

std::shared_ptr<ILogRecoverySink> make_default_fallback_sink() {
  return std::make_shared<RingBufferFallbackSink>();
}

std::string normalize_identifier(std::string value) {
  if (value.empty()) {
    return std::string(LogContext::kUnknownIdentifier);
  }

  return value;
}

}  // namespace

LoggingFacade::LoggingFacade()
    : fallback_sink_(make_default_fallback_sink()) {
  set_dispatch_backend(make_default_dispatch_backend());
}

LoggingFacade::LoggingFacade(std::unique_ptr<ILogDispatchBackend> dispatch_backend)
    : LoggingFacade(std::move(dispatch_backend), make_default_fallback_sink()) {}

LoggingFacade::LoggingFacade(std::unique_ptr<ILogDispatchBackend> dispatch_backend,
                             std::shared_ptr<ILogRecoverySink> fallback_sink)
    : fallback_sink_(std::move(fallback_sink)) {
  set_dispatch_backend(std::move(dispatch_backend));
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
  reset_recovery_path();

  lifecycle_state_ = LifecycleState::Initialized;
  return InfraOperationResult::success();
}

InfraOperationResult LoggingFacade::stop() {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return invalid_transition("stop", "initialized");
  }

    const auto flush_result = flush(
      LogFlushDeadline{.timeout_ms = kLoggingFacadeStopFlushTimeoutMs});
  if (!flush_result.ok) {
    return InfraOperationResult::failure(
        flush_result.result_code,
        "logging facade stop() could not drain the logging queue before the shutdown deadline",
        "logging.stop",
        std::string(kLoggingFacadeSourceRef));
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
  LogEvent formatted_event;
  try {
    if (force_format_failure_for_tests_) {
      throw std::runtime_error("forced formatter failure for tests");
    }
    formatted_event = structured_formatter_.format(redacted_event);
  } catch (...) {
    return recover_format_failure(redacted_event);
  }

  if (recovery_ != nullptr && recovery_->is_degraded()) {
    return handle_recovery_result(recovery_->write(formatted_event), formatted_event);
  }

  const auto result = dispatch_backend_->dispatch(formatted_event);
  if (!result.ok) {
    return handle_dispatch_failure(formatted_event, result);
  }

  last_dispatched_event_ = std::move(formatted_event);
  ++dispatched_record_count_;
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

  const auto flush_result = dispatch_backend_->flush(deadline);
  if (flush_result.ok || recovery_ == nullptr || !has_last_dispatched_event() ||
      flush_result.result_code != contracts::ResultCode::ProviderTimeout) {
    return flush_result;
  }

  return handle_recovery_result(
      recovery_->handle_sink_failure(
          last_dispatched_event(),
          "primary flush surfaced a sink failure after the deterministic queue drain path"),
      last_dispatched_event());
}

void LoggingFacade::set_level(LogLevel level) {
  current_level_ = level;
}

InfraOperationResult LoggingFacade::apply_config(const LoggingConfig& config) {
  if (lifecycle_state_ != LifecycleState::Initialized) {
    return invalid_transition("apply_config", "initialized");
  }

  current_level_ = config.level;
  redaction_filter_.set_options(RedactionFilterOptions{
      .enabled = config.redaction_enabled,
      .ruleset = config.redaction_ruleset,
  });
  structured_formatter_.set_options(StructuredFormatterOptions{
      .format = config.format,
  });
  return InfraOperationResult::success();
}

void LoggingFacade::set_dispatch_backend(
    std::unique_ptr<ILogDispatchBackend> dispatch_backend) {
  dispatch_backend_ = dispatch_backend != nullptr
                          ? std::move(dispatch_backend)
                          : make_default_dispatch_backend();
  reset_recovery_path();
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

LogWriteResult LoggingFacade::handle_recovery_result(
    const LoggingRecoveryResult& result,
    const LogEvent& primary_event) {
  if (!result.has_consistent_state()) {
    return LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "logging recovery returned an inconsistent fallback state",
        "logging.recovery",
        std::string(kLoggingFacadeSourceRef));
  }

  if (result.persisted) {
    if (recovery_ != nullptr && recovery_->has_last_fallback_event()) {
      last_dispatched_event_ = recovery_->last_fallback_event();
    } else {
      last_dispatched_event_ = primary_event;
    }
    ++dispatched_record_count_;
    return LogWriteResult::success();
  }

  const auto result_code = result.result_code.value_or(
      contracts::ResultCode::RuntimeRetryExhausted);
  return LogWriteResult::failure(
      result_code,
      "logging recovery could not persist the record through the degraded fallback path",
      "logging.recovery",
      std::string(kLoggingFacadeSourceRef));
}

LogWriteResult LoggingFacade::recover_format_failure(const LogEvent& event) {
  if (recovery_ == nullptr) {
    return LogWriteResult::failure(
        map_logging_error_code(LoggingErrorCode::FormatInvalid).result_code,
        "structured formatter failed before the logging recovery path was initialized",
        "logging.format",
        std::string(kLoggingFacadeSourceRef));
  }

  return handle_recovery_result(recovery_->handle_format_failure(event), event);
}

LogWriteResult LoggingFacade::handle_dispatch_failure(
    const LogEvent& formatted_event,
    const LogWriteResult& dispatch_result) {
  if (recovery_ == nullptr) {
    return dispatch_result;
  }

  if (dispatch_result.result_code == contracts::ResultCode::RuntimeRetryExhausted) {
    return handle_recovery_result(recovery_->handle_queue_saturation(formatted_event),
                                  formatted_event);
  }

  return handle_recovery_result(
      recovery_->handle_sink_failure(formatted_event,
                                     "primary dispatch failed before the record could be durably persisted"),
      formatted_event);
}

void LoggingFacade::reset_recovery_path() {
  if (fallback_sink_ == nullptr) {
    fallback_sink_ = make_default_fallback_sink();
  }

  if (dispatch_backend_ == nullptr) {
    recovery_.reset();
    return;
  }

  recovery_ = std::make_unique<LoggingRecovery>(
      std::make_shared<DispatchBackendRecoverySink>(dispatch_backend_.get()),
      fallback_sink_);
}

}  // namespace dasall::infra::logging
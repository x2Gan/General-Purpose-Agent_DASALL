#include "LoggingFacade.h"

#include <chrono>
#include <string_view>

#include "audit/IAuditLogger.h"
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
constexpr std::string_view kLoggingAuditActor = "infra.logging";
constexpr std::string_view kLoggingAuditAction = "logging.audit_route";
constexpr std::string_view kLoggingAuditEventIdPrefix = "logging-audit-event-";

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

[[nodiscard]] std::optional<std::string_view> find_attr(
    const LogEvent& event,
    std::string_view key) {
  const auto it = event.attrs.find(std::string(key));
  if (it == event.attrs.end()) {
    return std::nullopt;
  }

  return it->second;
}

[[nodiscard]] bool has_non_empty_attr(const LogEvent& event,
                                      std::string_view key) {
  const auto value = find_attr(event, key);
  return value.has_value() && !value->empty();
}

[[nodiscard]] bool attr_equals(const LogEvent& event,
                               std::string_view key,
                               std::string_view expected) {
  const auto value = find_attr(event, key);
  return value.has_value() && *value == expected;
}

[[nodiscard]] bool is_supported_evidence_kind(std::string_view evidence_kind) {
  return evidence_kind == "tool_result" ||
         evidence_kind == "recovery_outcome" ||
         evidence_kind == "worker_task";
}

[[nodiscard]] AuditEvidenceKind parse_evidence_kind(std::string_view evidence_kind) {
  if (evidence_kind == "tool_result") {
    return AuditEvidenceKind::ToolResult;
  }

  if (evidence_kind == "recovery_outcome") {
    return AuditEvidenceKind::RecoveryOutcome;
  }

  if (evidence_kind == "worker_task") {
    return AuditEvidenceKind::WorkerTask;
  }

  return AuditEvidenceKind::Unspecified;
}

[[nodiscard]] std::string normalized_attr_or(const LogEvent& event,
                                             std::string_view key,
                                             std::string_view fallback) {
  const auto value = find_attr(event, key);
  if (!value.has_value() || value->empty()) {
    return std::string(fallback);
  }

  return std::string(*value);
}

[[nodiscard]] std::string_view log_level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
    case LogLevel::Fatal:
      return "fatal";
    case LogLevel::Unspecified:
      break;
  }

  return "unknown";
}

[[nodiscard]] AuditOutcome audit_outcome_for(const LogEvent& event) {
  if (event.level == LogLevel::Fatal) {
    return AuditOutcome::Escalated;
  }

  if (event.level == LogLevel::Error || attr_equals(event, "event_kind", "high_risk") ||
      event.category() == "audit") {
    return AuditOutcome::Failed;
  }

  return AuditOutcome::Succeeded;
}

[[nodiscard]] std::string make_audit_target(const LogEvent& event) {
  std::string target = std::string("log_event:") +
                       (event.module.empty() ? std::string("unknown") : event.module);
  const auto event_kind = find_attr(event, "event_kind");
  if (event_kind.has_value() && !event_kind->empty()) {
    target += ":" + std::string(*event_kind);
    return target;
  }

  target += ":" + std::string(log_level_name(event.level));
  return target;
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

void LoggingFacade::attach_audit_logger(
    std::shared_ptr<audit::IAuditLogger> audit_logger) {
  audit_logger_ = std::move(audit_logger);
}

void LoggingFacade::attach_metrics_bridge(
    std::shared_ptr<LoggingMetricsBridge> metrics_bridge,
    std::uint32_t queue_high_watermark) {
  metrics_bridge_ = std::move(metrics_bridge);
  queue_high_watermark_ = std::max<std::uint32_t>(1U, queue_high_watermark);
  last_observed_dropped_total_ = current_dropped_total();
}

InfraOperationResult LoggingFacade::init(const LogContext& context) {
  if (lifecycle_state_ != LifecycleState::Created) {
    return invalid_transition("init", "created");
  }

  current_context_ = normalize_context(context);
  current_level_ = LogLevel::Info;
  last_dispatched_event_.reset();
  dispatched_record_count_ = 0;
  next_audit_event_sequence_ = 1U;

  if (!dispatch_backend_) {
    dispatch_backend_ = make_default_dispatch_backend();
  }
  reset_recovery_path();
  reset_runtime_health_state();

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

  const auto metric_ts = metric_timestamp_for(event);

  auto enriched_event = enrich_event(event);
  auto redacted_event = redaction_filter_.apply(enriched_event);
  LogEvent formatted_event;
  try {
    if (force_format_failure_for_tests_) {
      throw std::runtime_error("forced formatter failure for tests");
    }
    formatted_event = structured_formatter_.format(redacted_event);
  } catch (...) {
    record_write_failed(LoggingErrorCode::FormatInvalid, metric_ts, "write");
    const auto recovery_result = recover_format_failure(redacted_event);
    if (!recovery_result.ok) {
      note_unrecoverable_failure();
      if (recovery_ != nullptr) {
        record_write_failed(
            last_recovery_error_code().value_or(LoggingErrorCode::FormatInvalid),
            metric_ts,
            "recovery");
      }
      record_queue_depth(metric_ts, "failure");
      return recovery_result;
    }

    record_write_accepted(metric_ts);
    record_queue_depth(metric_ts, "degraded");
    return recovery_result;
  }

  if (requires_audit_handoff(redacted_event)) {
    const auto audit_result = persist_audit_record(redacted_event);
    if (!audit_result.ok) {
      const auto audit_error_code = audit_result.result_code ==
              contracts::ResultCode::ValidationFieldMissing
          ? LoggingErrorCode::ConfigInvalid
          : logging_error_code_for(audit_result, LoggingErrorCode::SinkIo);
      record_write_failed(audit_error_code, metric_ts, "write");
      record_queue_depth(metric_ts, "failure");
      return audit_result;
    }
  }

  if (recovery_ != nullptr && recovery_->is_degraded()) {
    const auto recovery_result =
        handle_recovery_result(recovery_->write(formatted_event), formatted_event);
    if (!recovery_result.ok) {
      note_unrecoverable_failure();
      record_write_failed(
          last_recovery_error_code().value_or(LoggingErrorCode::SinkIo),
          metric_ts,
          "recovery");
      record_queue_depth(metric_ts, "failure");
      return recovery_result;
    }

    record_write_accepted(metric_ts);
    record_queue_depth(metric_ts, "degraded");
    return recovery_result;
  }

  const auto dropped_total_before_dispatch = current_dropped_total();
  const auto result = dispatch_backend_->dispatch(formatted_event);
  if (!result.ok) {
    if (result.result_code == contracts::ResultCode::RuntimeRetryExhausted) {
      const auto recovery_result = handle_dispatch_failure(formatted_event, result);
      if (!recovery_result.ok) {
        note_unrecoverable_failure();
        record_drop(metric_ts, "failure");
        record_queue_depth(metric_ts, "failure");
        return recovery_result;
      }

      record_drop(metric_ts, "degraded");
      record_queue_depth(metric_ts, "degraded");
      return recovery_result;
    }

    record_write_failed(logging_error_code_for(result), metric_ts, "write");
    const auto recovery_result = handle_dispatch_failure(formatted_event, result);
    if (!recovery_result.ok) {
      note_unrecoverable_failure();
      if (recovery_ != nullptr) {
        record_write_failed(
            last_recovery_error_code().value_or(LoggingErrorCode::SinkIo),
            metric_ts,
            "recovery");
      }
      record_queue_depth(metric_ts, "failure");
      return recovery_result;
    }

    record_write_accepted(metric_ts);
    record_queue_depth(metric_ts, "degraded");
    return recovery_result;
  }

  last_dispatched_event_ = std::move(formatted_event);
  ++dispatched_record_count_;
  if (current_dropped_total() > dropped_total_before_dispatch) {
    record_drop(metric_ts, "degraded");
  }
  record_write_accepted(metric_ts);
  record_queue_depth(metric_ts, "success");
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

  const auto flush_started_at_steady_ms = current_steady_time_ms();

  const auto flush_result = dispatch_backend_->flush(deadline);
  if (flush_result.ok || recovery_ == nullptr || !has_last_dispatched_event() ||
      flush_result.result_code != contracts::ResultCode::ProviderTimeout) {
    if (!flush_result.ok) {
      note_unrecoverable_failure();
    }
    record_flush_latency(flush_started_at_steady_ms, flush_result);
    record_queue_depth(current_time_unix_ms(),
                       flush_result.ok
                           ? (is_degraded() ? "degraded" : "success")
                           : "failure");
    return flush_result;
  }

  const auto recovery_result = handle_recovery_result(
      recovery_->handle_sink_failure(
          last_dispatched_event(),
          "primary flush surfaced a sink failure after the deterministic queue drain path"),
      last_dispatched_event());
  if (!recovery_result.ok) {
    note_unrecoverable_failure();
  }
  record_flush_latency(flush_started_at_steady_ms, recovery_result);
  record_queue_depth(current_time_unix_ms(),
                     recovery_result.ok
                         ? (is_degraded() ? "degraded" : "success")
                         : "failure");
  return recovery_result;
}

LoggingHealthSample LoggingFacade::sample(std::int64_t timeout_ms) {
  const auto started_at_steady_ms = current_steady_time_ms();
  const auto sampled_at_unix_ms = current_time_unix_ms();

  if (timeout_ms <= 0) {
    return LoggingHealthSample{
        .state = LoggingHealthSampleState::Invalid,
        .signals = {},
        .latency_ms = 0,
        .sampled_at_unix_ms = sampled_at_unix_ms,
        .detail_ref = std::string(kLoggingHealthDetailNamespace) +
            "/config/timeout_invalid",
    };
  }

  if (lifecycle_state_ != LifecycleState::Initialized) {
    return LoggingHealthSample{
        .state = LoggingHealthSampleState::Invalid,
        .signals = {},
        .latency_ms = std::max<std::int64_t>(0,
                                             current_steady_time_ms() -
                                                 started_at_steady_ms),
        .sampled_at_unix_ms = sampled_at_unix_ms,
        .detail_ref = std::string(kLoggingHealthDetailNamespace) +
            "/lifecycle/invalid",
    };
  }

  const auto current_dropped_total = this->current_dropped_total();
  const auto dropped_total_delta =
      current_dropped_total >= last_observed_dropped_total_
          ? current_dropped_total - last_observed_dropped_total_
          : current_dropped_total;
  last_observed_dropped_total_ = current_dropped_total;

  return LoggingHealthSample{
      .state = LoggingHealthSampleState::Ready,
      .signals = LoggingHealthSignals{
          .queue_depth = current_queue_depth(),
          .queue_high_watermark = std::max<std::uint32_t>(1U, queue_high_watermark_),
          .dropped_total_delta = dropped_total_delta,
          .recovery_degraded = is_degraded(),
          .fallback_active = fallback_active(),
          .unrecoverable_failure_total = unrecoverable_failure_total_,
          .metrics_bridge_degraded = metrics_bridge_ != nullptr &&
              metrics_bridge_->is_degraded(),
      },
      .latency_ms = std::max<std::int64_t>(0,
                                           current_steady_time_ms() -
                                               started_at_steady_ms),
      .sampled_at_unix_ms = sampled_at_unix_ms,
      .detail_ref = std::string(kLoggingHealthDetailNamespace) + "/sample",
  };
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
  reset_runtime_health_state();
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

bool LoggingFacade::requires_audit_handoff(const LogEvent& event) {
  return event.category() == "audit" || event.level == LogLevel::Fatal ||
         attr_equals(event, "event_kind", "high_risk");
}

bool LoggingFacade::has_complete_audit_anchor_attrs(const LogEvent& event) {
  const auto evidence_kind = find_attr(event, "evidence_kind");
  return attr_equals(event, "audit_ref_pending", "true") &&
         has_non_empty_attr(event, "evidence_ref") &&
         evidence_kind.has_value() && is_supported_evidence_kind(*evidence_kind) &&
         has_non_empty_attr(event, "audit_trace_id") &&
         has_non_empty_attr(event, "audit_task_id");
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

AuditContext LoggingFacade::make_audit_context(const LogEvent& event) const {
  return AuditContext{
      .request_id = normalized_attr_or(event,
                                       "request_id",
                                       current_context_.request_id),
      .session_id = normalized_attr_or(event,
                                       "session_id",
                                       current_context_.session_id),
      .trace_id = normalized_attr_or(event,
                                     "audit_trace_id",
                                     current_context_.trace_id),
      .task_id = normalized_attr_or(event,
                                    "audit_task_id",
                                    current_context_.task_id),
      .parent_task_id = normalized_attr_or(event,
                                           "parent_task_id",
                                           current_context_.parent_task_id),
      .lease_id = normalized_attr_or(event,
                                     "lease_id",
                                     current_context_.lease_id),
      .worker_type = event.module.empty() ? std::string(kLoggingAuditActor)
                                          : event.module,
  };
}

AuditEvent LoggingFacade::make_audit_event(const LogEvent& event) {
  AuditEvent::SideEffects side_effects;
  side_effects.push_back(std::string("route:audit"));
  side_effects.push_back(std::string("log_level:") +
                         std::string(log_level_name(event.level)));
  if (const auto event_kind = find_attr(event, "event_kind");
      event_kind.has_value() && !event_kind->empty()) {
    side_effects.push_back(std::string("event_kind:") + std::string(*event_kind));
  }

  return AuditEvent{
      .event_id = std::string(kLoggingAuditEventIdPrefix) +
          std::to_string(next_audit_event_sequence_++),
      .action = std::string(kLoggingAuditAction),
      .actor = std::string(kLoggingAuditActor),
      .target = make_audit_target(event),
      .outcome = audit_outcome_for(event),
      .evidence_ref = {
          .kind = parse_evidence_kind(find_attr(event, "evidence_kind").value_or("")),
          .ref = normalized_attr_or(event,
                                    "evidence_ref",
                                    LogContext::kUnknownIdentifier),
      },
      .side_effects = std::move(side_effects),
      .timestamp = metric_timestamp_for(event),
  };
}

LogWriteResult LoggingFacade::persist_audit_record(const LogEvent& event) {
  if (!requires_audit_handoff(event)) {
    return LogWriteResult::success();
  }

  if (!has_complete_audit_anchor_attrs(event)) {
    return LogWriteResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "high-risk log requires complete audit correlation attrs before dispatch",
        "logging.audit_handoff",
        std::string(kLoggingFacadeSourceRef));
  }

  if (audit_logger_ == nullptr) {
    return LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "high-risk log requires an attached audit logger before dispatch",
        "logging.audit_handoff",
        std::string(kLoggingFacadeSourceRef));
  }

  const auto write_outcome = audit_logger_->write_audit(make_audit_event(event),
                                                        make_audit_context(event));
  if (write_outcome.is_success() || write_outcome.is_degraded_success()) {
    return LogWriteResult::success();
  }

  return LogWriteResult::failure(
      write_outcome.error_code.value_or(contracts::ResultCode::RuntimeRetryExhausted),
      "audit logger could not persist the correlated high-risk event",
      "logging.audit_handoff",
      std::string(kLoggingFacadeSourceRef));
}

std::int64_t LoggingFacade::current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::int64_t LoggingFacade::current_steady_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::uint32_t LoggingFacade::current_queue_depth() const {
  const auto* sink_dispatcher =
      dynamic_cast<const SinkDispatcher*>(dispatch_backend_.get());
  if (sink_dispatcher == nullptr) {
    return 0U;
  }

  return static_cast<std::uint32_t>(sink_dispatcher->queue_depth());
}

std::uint64_t LoggingFacade::current_dropped_total() const {
  const auto* sink_dispatcher =
      dynamic_cast<const SinkDispatcher*>(dispatch_backend_.get());
  if (sink_dispatcher == nullptr) {
    return 0U;
  }

  return sink_dispatcher->dropped_total();
}

LoggingErrorCode LoggingFacade::logging_error_code_for(
    const LogWriteResult& result,
    LoggingErrorCode fallback) {
  if (result.result_code == map_logging_error_code(LoggingErrorCode::QueueFull).result_code) {
    return LoggingErrorCode::QueueFull;
  }

  if (result.result_code == map_logging_error_code(LoggingErrorCode::ConfigInvalid).result_code) {
    return LoggingErrorCode::ConfigInvalid;
  }

  if (result.result_code == map_logging_error_code(LoggingErrorCode::FormatInvalid).result_code) {
    return LoggingErrorCode::FormatInvalid;
  }

  return fallback;
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

std::int64_t LoggingFacade::metric_timestamp_for(const LogEvent& event) const {
  return event.ts.has_value() && *event.ts > 0 ? *event.ts : current_time_unix_ms();
}

void LoggingFacade::note_unrecoverable_failure() {
  ++unrecoverable_failure_total_;
}

void LoggingFacade::record_write_accepted(std::int64_t ts_unix_ms) {
  if (metrics_bridge_ == nullptr) {
    return;
  }

  metrics_bridge_->record_write_accepted(ts_unix_ms);
}

void LoggingFacade::record_write_failed(LoggingErrorCode error_code,
                                        std::int64_t ts_unix_ms,
                                        std::string_view stage) {
  if (metrics_bridge_ == nullptr) {
    return;
  }

  metrics_bridge_->record_write_failed(error_code, ts_unix_ms, stage);
}

void LoggingFacade::record_drop(std::int64_t ts_unix_ms,
                                std::string_view outcome) {
  if (metrics_bridge_ == nullptr) {
    return;
  }

  metrics_bridge_->record_drop(LoggingErrorCode::QueueFull, ts_unix_ms, outcome);
}

void LoggingFacade::record_queue_depth(std::int64_t ts_unix_ms,
                                       std::string_view outcome) {
  if (metrics_bridge_ == nullptr) {
    return;
  }

  metrics_bridge_->record_queue_depth(current_queue_depth(), ts_unix_ms, outcome);
}

void LoggingFacade::record_flush_latency(std::int64_t started_at_steady_ms,
                                         const LogWriteResult& result) {
  if (metrics_bridge_ == nullptr) {
    return;
  }

  const auto finished_at_unix_ms = current_time_unix_ms();
  const auto latency_ms = std::max<std::int64_t>(0,
                                                 current_steady_time_ms() -
                                                     started_at_steady_ms);
  const auto outcome = !result.ok ? std::string_view("failure")
                                  : (is_degraded() ? std::string_view("degraded")
                                                   : std::string_view("success"));
  const auto error_code = outcome == "success"
      ? std::optional<LoggingErrorCode>{}
      : std::optional<LoggingErrorCode>(
            last_recovery_error_code().value_or(logging_error_code_for(result)));
  metrics_bridge_->record_flush_latency(latency_ms,
                                        finished_at_unix_ms,
                                        outcome,
                                        error_code);
}

void LoggingFacade::reset_runtime_health_state() {
  unrecoverable_failure_total_ = 0U;
  last_observed_dropped_total_ = current_dropped_total();
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
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "IInfrastructureService.h"
#include "logging/ILogConfigurator.h"
#include "logging/ILogger.h"
#include "logging/LoggingRecovery.h"
#include "logging/LogTypes.h"
#include "logging/RedactionFilter.h"
#include "logging/StructuredFormatter.h"

namespace dasall::infra::logging {

class ILogDispatchBackend {
 public:
  virtual ~ILogDispatchBackend() = default;

  virtual LogWriteResult dispatch(const LogEvent& event) = 0;
  virtual LogWriteResult flush(const LogFlushDeadline& deadline) = 0;
};

class LoggingFacade final : public ILogger {
 public:
  LoggingFacade();
  explicit LoggingFacade(std::unique_ptr<ILogDispatchBackend> dispatch_backend);
  LoggingFacade(std::unique_ptr<ILogDispatchBackend> dispatch_backend,
                std::shared_ptr<ILogRecoverySink> fallback_sink);

  InfraOperationResult init(const LogContext& context = {});
  InfraOperationResult stop();

  LogWriteResult log(const LogEvent& event) override;
  LogWriteResult flush(const LogFlushDeadline& deadline) override;
  void set_level(LogLevel level) override;
  InfraOperationResult apply_config(const LoggingConfig& config);

  void set_dispatch_backend(std::unique_ptr<ILogDispatchBackend> dispatch_backend);

  [[nodiscard]] const ILogDispatchBackend* dispatch_backend() const {
    return dispatch_backend_.get();
  }

  [[nodiscard]] std::size_t dispatched_record_count() const {
    return dispatched_record_count_;
  }

  [[nodiscard]] bool has_last_dispatched_event() const {
    return last_dispatched_event_.has_value();
  }

  [[nodiscard]] const LogEvent& last_dispatched_event() const {
    return *last_dispatched_event_;
  }

  [[nodiscard]] const LogContext& current_context() const {
    return current_context_;
  }

  void set_force_format_failure_for_tests(bool enabled) {
    force_format_failure_for_tests_ = enabled;
  }

  [[nodiscard]] LogLevel current_level() const {
    return current_level_;
  }

  [[nodiscard]] bool is_degraded() const {
    return recovery_ != nullptr && recovery_->is_degraded();
  }

  [[nodiscard]] bool fallback_active() const {
    return recovery_ != nullptr && recovery_->fallback_active();
  }

  [[nodiscard]] std::optional<LoggingErrorCode> last_recovery_error_code() const {
    return recovery_ != nullptr ? recovery_->last_error_code() : std::nullopt;
  }

  [[nodiscard]] bool has_last_fallback_event() const {
    return recovery_ != nullptr && recovery_->has_last_fallback_event();
  }

  [[nodiscard]] const LogEvent& last_fallback_event() const {
    return recovery_->last_fallback_event();
  }

  [[nodiscard]] LoggingFormat current_format() const {
    return structured_formatter_.format_type();
  }

  [[nodiscard]] bool redaction_enabled() const {
    return redaction_filter_.enabled();
  }

  [[nodiscard]] const std::string& redaction_ruleset() const {
    return redaction_filter_.ruleset();
  }

  [[nodiscard]] std::string_view lifecycle_state_name() const;

 private:
  enum class LifecycleState {
    Created,
    Initialized,
    Stopped,
  };

  [[nodiscard]] InfraOperationResult invalid_transition(
      std::string_view operation,
      std::string_view expected_state) const;
  [[nodiscard]] static LogContext normalize_context(LogContext context);
  [[nodiscard]] static bool is_enabled_for_level(LogLevel event_level,
                                                 LogLevel current_level);
  [[nodiscard]] LogEvent enrich_event(const LogEvent& event) const;
  [[nodiscard]] LogWriteResult handle_recovery_result(
      const LoggingRecoveryResult& result,
      const LogEvent& primary_event);
  [[nodiscard]] LogWriteResult recover_format_failure(const LogEvent& event);
  [[nodiscard]] LogWriteResult handle_dispatch_failure(
      const LogEvent& formatted_event,
      const LogWriteResult& dispatch_result);
  void reset_recovery_path();

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  LogContext current_context_{};
  LogLevel current_level_ = LogLevel::Info;
  RedactionFilter redaction_filter_{};
  StructuredFormatter structured_formatter_{};
  std::unique_ptr<ILogDispatchBackend> dispatch_backend_;
  std::shared_ptr<ILogRecoverySink> fallback_sink_;
  std::unique_ptr<LoggingRecovery> recovery_;
  bool force_format_failure_for_tests_ = false;
  std::optional<LogEvent> last_dispatched_event_;
  std::size_t dispatched_record_count_ = 0;
};

}  // namespace dasall::infra::logging
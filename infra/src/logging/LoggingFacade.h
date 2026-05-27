#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "IInfrastructureService.h"
#include "logging/ILogger.h"
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

  InfraOperationResult init(const LogContext& context = {});
  InfraOperationResult stop();

  LogWriteResult log(const LogEvent& event) override;
  LogWriteResult flush(const LogFlushDeadline& deadline) override;
  void set_level(LogLevel level) override;

  void set_dispatch_backend(std::unique_ptr<ILogDispatchBackend> dispatch_backend);

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

  [[nodiscard]] LogLevel current_level() const {
    return current_level_;
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

  LifecycleState lifecycle_state_ = LifecycleState::Created;
  LogContext current_context_{};
  LogLevel current_level_ = LogLevel::Info;
  RedactionFilter redaction_filter_{};
  StructuredFormatter structured_formatter_{};
  std::unique_ptr<ILogDispatchBackend> dispatch_backend_;
  std::optional<LogEvent> last_dispatched_event_;
  std::size_t dispatched_record_count_ = 0;
};

}  // namespace dasall::infra::logging
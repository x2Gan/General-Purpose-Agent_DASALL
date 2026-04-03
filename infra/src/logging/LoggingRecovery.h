#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "logging/ILogger.h"
#include "logging/LoggingErrors.h"
#include "logging/LogTypes.h"

namespace dasall::infra::logging {

class ILogRecoverySink {
 public:
  virtual ~ILogRecoverySink() = default;

  virtual LogWriteResult write(const LogEvent& event) = 0;
};

struct LoggingRecoveryResult {
  bool accepted = false;
  bool persisted = false;
  bool fallback_used = false;
  bool degraded = false;
  bool recovery_attempted = false;
  bool recovery_succeeded = false;
  std::optional<LoggingErrorCode> logging_error_code;
  std::optional<contracts::ResultCode> result_code;

  [[nodiscard]] bool has_consistent_state() const {
    if (persisted && result_code.has_value()) {
      return false;
    }

    if (!persisted && !result_code.has_value()) {
      return false;
    }

    if (recovery_succeeded && !recovery_attempted) {
      return false;
    }

    if (recovery_succeeded && degraded) {
      return false;
    }

    if (fallback_used && !degraded) {
      return false;
    }

    return true;
  }

  [[nodiscard]] bool is_success() const {
    return accepted && persisted && !degraded && !result_code.has_value();
  }

  [[nodiscard]] bool is_degraded_success() const {
    return accepted && persisted && degraded && fallback_used &&
           !result_code.has_value();
  }

  [[nodiscard]] bool is_failure() const {
    return !persisted && result_code.has_value();
  }
};

class LoggingRecovery {
 public:
  LoggingRecovery(std::shared_ptr<ILogRecoverySink> primary_sink,
                  std::shared_ptr<ILogRecoverySink> fallback_sink);

  LoggingRecoveryResult write(const LogEvent& event);
  LoggingRecoveryResult handle_format_failure(const LogEvent& event);
  LoggingRecoveryResult retry_primary_sink(const LogEvent& probe_event);

  [[nodiscard]] bool is_degraded() const {
    return degraded_;
  }

  [[nodiscard]] bool fallback_active() const {
    return fallback_active_;
  }

  [[nodiscard]] std::uint64_t retry_attempt_total() const {
    return retry_attempt_total_;
  }

  [[nodiscard]] std::uint64_t recovery_success_total() const {
    return recovery_success_total_;
  }

  [[nodiscard]] std::uint64_t recovery_failure_total() const {
    return recovery_failure_total_;
  }

  [[nodiscard]] const std::string& last_failure_reason() const {
    return last_failure_reason_;
  }

  [[nodiscard]] std::optional<LoggingErrorCode> last_error_code() const {
    return last_error_code_;
  }

  [[nodiscard]] bool has_last_fallback_event() const {
    return last_fallback_event_.has_value();
  }

  [[nodiscard]] const LogEvent& last_fallback_event() const {
    return *last_fallback_event_;
  }

 private:
  [[nodiscard]] LoggingRecoveryResult write_to_fallback(
      const LogEvent& event,
      LoggingErrorCode error_code,
      std::string_view reason,
      bool recovery_attempted);
  [[nodiscard]] static LoggingRecoveryResult make_failure_result(
      LoggingErrorCode error_code,
      std::string message,
      std::string stage,
      bool fallback_used,
      bool degraded,
      bool recovery_attempted);
  [[nodiscard]] static LogEvent make_minimal_fallback_event(const LogEvent& event);

  std::shared_ptr<ILogRecoverySink> primary_sink_;
  std::shared_ptr<ILogRecoverySink> fallback_sink_;
  bool degraded_ = false;
  bool fallback_active_ = false;
  std::uint64_t retry_attempt_total_ = 0;
  std::uint64_t recovery_success_total_ = 0;
  std::uint64_t recovery_failure_total_ = 0;
  std::string last_failure_reason_;
  std::optional<LoggingErrorCode> last_error_code_;
  std::optional<LogEvent> last_fallback_event_;
};

}  // namespace dasall::infra::logging
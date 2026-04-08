#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "watchdog/IWatchdogService.h"
#include "watchdog/TimeoutDecision.h"
#include "watchdog/WatchdogErrors.h"

namespace dasall::infra::watchdog {

struct TimeoutEvent {
  std::string event_id;
  std::string entity_id;
  WatchdogTimeoutLevel timeout_level = WatchdogTimeoutLevel::Unspecified;
  std::uint32_t consecutive_miss = 0;
  contracts::ResultCode reason_code =
      contracts::ResultCode::RuntimeRetryExhausted;
  std::string evidence_ref;
  std::string trace_id = "unknown";
  std::string session_id = "unknown";
  std::string task_id = "unknown";

  [[nodiscard]] bool has_required_fields() const {
    return !event_id.empty() && !entity_id.empty() &&
           timeout_level != WatchdogTimeoutLevel::Unspecified &&
           consecutive_miss > 0 &&
           contracts::classify_result_code(reason_code) !=
               contracts::ResultCodeCategory::Unknown &&
           !evidence_ref.empty() && !trace_id.empty() && !session_id.empty() &&
           !task_id.empty();
  }
};

struct TimeoutEventDispatchResult {
  bool published = false;
  std::string delivery_ref;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static TimeoutEventDispatchResult success(
      std::string delivery_ref) {
    return TimeoutEventDispatchResult{
        .published = true,
        .delivery_ref = std::move(delivery_ref),
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static TimeoutEventDispatchResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return TimeoutEventDispatchResult{
        .published = false,
        .delivery_ref = {},
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return published;
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }
};

class ITimeoutEventSink {
 public:
  virtual ~ITimeoutEventSink() = default;

  virtual TimeoutEventDispatchResult publish_timeout_event(
      const TimeoutEvent& event) = 0;
};

struct TimeoutEventPublishResult {
  bool emitted = false;
  bool buffered = false;
  bool skipped = false;
  TimeoutEvent event{};
  std::string delivery_ref;
  std::size_t buffered_event_count = 0;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static TimeoutEventPublishResult success(
      TimeoutEvent event,
      std::string delivery_ref,
      std::size_t buffered_event_count) {
    return TimeoutEventPublishResult{
        .emitted = true,
        .buffered = false,
        .skipped = false,
        .event = std::move(event),
        .delivery_ref = std::move(delivery_ref),
        .buffered_event_count = buffered_event_count,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static TimeoutEventPublishResult skip(
      std::size_t buffered_event_count) {
    return TimeoutEventPublishResult{
        .emitted = false,
        .buffered = false,
        .skipped = true,
        .event = {},
        .delivery_ref = {},
        .buffered_event_count = buffered_event_count,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static TimeoutEventPublishResult failure(
      TimeoutEvent event,
      bool buffered,
      std::size_t buffered_event_count,
      WatchdogErrorCode watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return TimeoutEventPublishResult{
        .emitted = false,
        .buffered = buffered,
        .skipped = false,
        .event = std::move(event),
        .delivery_ref = {},
        .buffered_event_count = buffered_event_count,
        .watchdog_code = watchdog_code,
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
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return emitted || skipped;
    }

    return result_code.has_value() && error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool is_valid() const {
    if (emitted) {
      return !buffered && !skipped && event.has_required_fields() &&
             !delivery_ref.empty() && !result_code.has_value() &&
             !error_info.has_value();
    }

    if (skipped) {
      return !buffered && !result_code.has_value() && !error_info.has_value();
    }

    return !emitted && !skipped && result_code.has_value() &&
           error_info.has_value() && references_only_contract_error_types();
  }
};

struct TimeoutEventPublisherStatus {
  std::uint64_t published_total = 0;
  std::uint64_t skipped_total = 0;
  std::uint64_t publish_fail_total = 0;
  std::uint64_t buffered_total = 0;
  std::uint64_t dropped_total = 0;
  bool degraded = false;
  std::optional<WatchdogErrorCode> last_watchdog_code;
  std::optional<contracts::ResultCode> last_result_code;
  std::size_t buffered_event_count = 0;

  [[nodiscard]] bool is_valid() const {
    if (last_result_code.has_value() &&
        contracts::classify_result_code(*last_result_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    if (!last_result_code.has_value() && last_watchdog_code.has_value()) {
      return false;
    }

    return true;
  }
};

struct TimeoutEventPublisherOptions {
  std::string default_trace_id = "unknown";
  std::string default_session_id = "unknown";
  std::string default_task_id = "unknown";
};

class TimeoutEventPublisher {
 public:
  explicit TimeoutEventPublisher(
      std::shared_ptr<ITimeoutEventSink> event_sink = nullptr,
      WatchdogServiceConfig config = {},
      TimeoutEventPublisherOptions options = {});

  void set_event_sink(std::shared_ptr<ITimeoutEventSink> event_sink);

  [[nodiscard]] bool has_event_sink() const {
    return static_cast<bool>(event_sink_);
  }

  [[nodiscard]] TimeoutEventPublishResult publish_timeout(
      const TimeoutDecision& decision);
  [[nodiscard]] TimeoutEventPublisherStatus status() const;

 private:
  [[nodiscard]] static bool should_publish(const TimeoutDecision& decision);
  [[nodiscard]] TimeoutEvent build_event(const TimeoutDecision& decision) const;
  [[nodiscard]] bool buffer_event(TimeoutEvent event);

  void record_success();
  void record_skip();
  void record_failure(WatchdogErrorCode watchdog_code,
                      contracts::ResultCode result_code,
                      bool buffered,
                      bool dropped);

  std::shared_ptr<ITimeoutEventSink> event_sink_;
  WatchdogServiceConfig config_;
  TimeoutEventPublisherOptions options_;
  std::deque<TimeoutEvent> fallback_ring_;
  std::uint64_t published_total_ = 0;
  std::uint64_t skipped_total_ = 0;
  std::uint64_t publish_fail_total_ = 0;
  std::uint64_t buffered_total_ = 0;
  std::uint64_t dropped_total_ = 0;
  std::optional<WatchdogErrorCode> last_watchdog_code_;
  std::optional<contracts::ResultCode> last_result_code_;
};

}  // namespace dasall::infra::watchdog
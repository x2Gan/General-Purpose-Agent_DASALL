#include "watchdog/DeadlineWheel.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "ITimer.h"
#include "PlatformError.h"

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kDeadlineWheelSourceRef = "DeadlineWheel";

[[nodiscard]] contracts::ResultCode map_platform_result_code(
    const platform::PlatformError& error) {
  switch (platform::map_platform_error_category_to_contracts(error.category)) {
    case contracts::ResultCodeCategory::Validation:
      return contracts::ResultCode::ValidationFieldMissing;
    case contracts::ResultCodeCategory::Provider:
      return error.code == platform::PlatformErrorCode::Timeout
                 ? contracts::ResultCode::ProviderTimeout
                 : contracts::ResultCode::ToolExecutionFailed;
    case contracts::ResultCodeCategory::Runtime:
    case contracts::ResultCodeCategory::Policy:
    case contracts::ResultCodeCategory::Tool:
    case contracts::ResultCodeCategory::Unknown:
      break;
  }

  return contracts::ResultCode::RuntimeRetryExhausted;
}

}  // namespace

DeadlineWheel::DeadlineWheel(WatchdogServiceConfig config,
                             const HeartbeatRegistry* registry,
                             const HeartbeatIngestor* ingestor,
                             std::shared_ptr<platform::ITimer> scan_scheduler)
    : config_(std::move(config)),
      registry_(registry),
      ingestor_(ingestor),
      scan_scheduler_(std::move(scan_scheduler)) {}

void DeadlineWheel::bind_registry(const HeartbeatRegistry* registry) {
  registry_ = registry;
}

void DeadlineWheel::bind_ingestor(const HeartbeatIngestor* ingestor) {
  ingestor_ = ingestor;
}

void DeadlineWheel::set_scan_scheduler(
    std::shared_ptr<platform::ITimer> scan_scheduler) {
  scan_scheduler_ = std::move(scan_scheduler);
}

DeadlineScanResult DeadlineWheel::tick_collect_due(std::int64_t now_ts) {
  if (now_ts <= 0) {
    return invalid_request(
        "deadline wheel requires a positive monotonic now_ts for tick_collect_due",
        "watchdog.deadline.tick_collect_due",
        now_ts,
        0,
        false,
        safe_observe_mode_);
  }

  if (!config_.is_valid()) {
    return invalid_request(
        "deadline wheel requires a valid watchdog service config before scanning",
        "watchdog.deadline.tick_collect_due",
        now_ts,
        0,
        false,
        safe_observe_mode_);
  }

  if (registry_ == nullptr || ingestor_ == nullptr) {
    return invalid_request(
        "deadline wheel requires bound registry and ingestor dependencies before scanning",
        "watchdog.deadline.tick_collect_due",
        now_ts,
        0,
        false,
        safe_observe_mode_);
  }

  if (safe_observe_mode_) {
    return scan_overdue(
        now_ts,
        0,
        false,
        "deadline wheel remains in safe_observe_mode after a prior overdue scan");
  }

  const auto previous_scan_ts = last_scan_ts_;
  std::int64_t scan_lag_ms = 0;
  if (previous_scan_ts > 0) {
    const auto expected_next_scan =
        previous_scan_ts + static_cast<std::int64_t>(config_.scan_interval_ms);
    scan_lag_ms = std::max<std::int64_t>(0, now_ts - expected_next_scan);

    if (now_ts - previous_scan_ts >
        static_cast<std::int64_t>(config_.safe_mode_scan_interval_ms)) {
      return scan_overdue(
          now_ts,
          scan_lag_ms,
          false,
          "deadline wheel observed a scan gap beyond safe_mode_scan_interval_ms");
    }
  }

  std::vector<DeadlineCandidate> due_candidates;
  for (const auto& descriptor : registry_->list_entities()) {
    const auto latest = ingestor_->latest_sample(descriptor.entity_id);
    if (!latest.ok || !latest.has_sample) {
      continue;
    }

    if (now_ts < latest.sample.deadline_ts) {
      continue;
    }

    due_candidates.push_back(DeadlineCandidate{
        .descriptor = descriptor,
        .latest_sample = latest.sample,
        .overdue_by_ms = now_ts - latest.sample.deadline_ts,
    });
  }

  last_scan_ts_ = now_ts;
  return DeadlineScanResult::success(
      std::move(due_candidates),
      now_ts,
      scan_lag_ms,
      false,
      safe_observe_mode_);
}

DeadlineScanResult DeadlineWheel::scan_once() {
  if (!config_.is_valid()) {
    return invalid_request(
        "deadline wheel requires a valid watchdog service config before scan_once",
        "watchdog.deadline.scan_once",
        0,
        0,
        false,
        safe_observe_mode_);
  }

  if (scan_scheduler_ == nullptr) {
    return invalid_request(
        "deadline wheel requires a platform ITimer scheduler before scan_once",
        "watchdog.deadline.scan_once",
        0,
        0,
        false,
        safe_observe_mode_);
  }

  bool scheduler_started = false;
  if (!scheduler_armed()) {
    platform::TimerSpec periodic_spec;
    periodic_spec.mode = platform::TimerMode::Periodic;
    periodic_spec.interval_ms = config_.scan_interval_ms;
    periodic_spec.initial_delay_ms = config_.scan_interval_ms;
    periodic_spec.clock_kind = platform::TimerClockKind::Monotonic;

    const auto started = scan_scheduler_->start_periodic(
        periodic_spec,
        [](const platform::TimerDriftStats&) {});
    if (!started.ok()) {
      return platform_failure(
          std::string("deadline wheel could not arm periodic scan scheduler: ") +
              started.error->detail,
          "watchdog.deadline.scan_once",
          0,
          0,
          false,
          safe_observe_mode_,
          map_platform_result_code(*started.error));
    }

    scheduled_scan_handle_id_ = started.value->native_id;
    scheduler_started = true;
  }

  auto result = tick_collect_due(expected_next_scan_ts());
  result.scheduler_started = scheduler_started;
  return result;
}

DeadlineScanResult DeadlineWheel::invalid_request(std::string message,
                                                  std::string stage,
                                                  std::int64_t scan_ts,
                                                  std::int64_t scan_lag_ms,
                                                  bool scheduler_started,
                                                  bool safe_observe_mode) const {
  return DeadlineScanResult::failure(
      std::nullopt,
      contracts::ResultCode::ValidationFieldMissing,
      std::move(message),
      std::move(stage),
      std::string(kDeadlineWheelSourceRef),
      scan_ts,
      scan_lag_ms,
      scheduler_started,
      safe_observe_mode);
}

DeadlineScanResult DeadlineWheel::platform_failure(
    std::string message,
    std::string stage,
    std::int64_t scan_ts,
    std::int64_t scan_lag_ms,
    bool scheduler_started,
    bool safe_observe_mode,
    contracts::ResultCode result_code) const {
  return DeadlineScanResult::failure(
      std::nullopt,
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kDeadlineWheelSourceRef),
      scan_ts,
      scan_lag_ms,
      scheduler_started,
      safe_observe_mode);
}

DeadlineScanResult DeadlineWheel::scan_overdue(std::int64_t now_ts,
                                               std::int64_t scan_lag_ms,
                                               bool scheduler_started,
                                               std::string detail_suffix) {
  safe_observe_mode_ = true;
  const auto mapping = map_watchdog_error_code(WatchdogErrorCode::ScanOverdue);
  return DeadlineScanResult::failure(
      WatchdogErrorCode::ScanOverdue,
      mapping.result_code,
      std::string(watchdog_error_code_name(WatchdogErrorCode::ScanOverdue)) +
          ": " + std::move(detail_suffix),
      "watchdog.deadline.tick_collect_due",
      std::string(kDeadlineWheelSourceRef),
      now_ts,
      scan_lag_ms,
      scheduler_started,
      true);
}

std::int64_t DeadlineWheel::expected_next_scan_ts() const {
  if (last_scan_ts_ == 0) {
    return static_cast<std::int64_t>(config_.scan_interval_ms);
  }

  return last_scan_ts_ + static_cast<std::int64_t>(config_.scan_interval_ms);
}

}  // namespace dasall::infra::watchdog
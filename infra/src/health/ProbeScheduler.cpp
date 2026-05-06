#include "health/ProbeScheduler.h"

#include <string_view>
#include <utility>

#include "PlatformError.h"

namespace dasall::infra {
namespace {

constexpr std::string_view kProbeSchedulerSourceRef = "ProbeScheduler";
constexpr std::string_view kLivenessGroup = "liveness";
constexpr std::string_view kReadinessGroup = "readiness";
constexpr std::string_view kTimerFallbackReason =
    "health.probe_scheduler://timer-unavailable";

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

[[nodiscard]] platform::TimerSpec make_periodic_spec(const std::uint32_t interval_ms) {
  return platform::TimerSpec{
      .mode = platform::TimerMode::Periodic,
      .interval_ms = interval_ms,
      .initial_delay_ms = interval_ms,
      .clock_kind = platform::TimerClockKind::Monotonic,
  };
}

}  // namespace

ProbeScheduler::ProbeScheduler(HealthResolvedConfig config,
                               std::shared_ptr<platform::ITimer> timer)
    : config_(config.is_valid() ? std::move(config) : HealthResolvedConfig{}),
      timer_(std::move(timer)) {}

ProbeSchedulerStartResult ProbeScheduler::start(TickCallback tick_callback) {
  if (!config_.is_valid()) {
    return ProbeSchedulerStartResult{
        .started = false,
        .fallback_active = fallback_active_,
        .status = invalid_request(
            "probe scheduler requires a valid health cadence projection before start",
            "health.scheduler.start"),
    };
  }

  tick_callback_ = std::move(tick_callback);

  if (!config_.enabled) {
    fallback_active_ = false;
    fallback_reason_.clear();
    return ProbeSchedulerStartResult{
        .started = false,
        .fallback_active = false,
        .status = HealthOperationStatus::success(),
    };
  }

  if (running()) {
    return ProbeSchedulerStartResult{
        .started = true,
        .fallback_active = fallback_active_,
        .status = HealthOperationStatus::success(),
    };
  }

  if (timer_ == nullptr) {
    fallback_active_ = true;
    fallback_reason_ = std::string(kTimerFallbackReason);
    return ProbeSchedulerStartResult{
        .started = false,
        .fallback_active = true,
        .status = HealthOperationStatus::success(),
    };
  }

  const auto liveness_started = timer_->start_periodic(
      make_periodic_spec(config_.liveness_interval_ms),
      [this](const platform::TimerDriftStats&) {
        std::vector<std::string> ignored;
        dispatch_group(kLivenessGroup, &ignored);
      });
  if (!liveness_started.ok()) {
    return ProbeSchedulerStartResult{
        .started = false,
        .fallback_active = false,
        .status = platform_failure(
            std::string("probe scheduler could not arm liveness timer: ") +
                liveness_started.error->detail,
            "health.scheduler.start",
            map_platform_result_code(*liveness_started.error)),
    };
  }

  liveness_handle_ = *liveness_started.value;
  const auto readiness_started = timer_->start_periodic(
      make_periodic_spec(config_.readiness_interval_ms),
      [this](const platform::TimerDriftStats&) {
        std::vector<std::string> ignored;
        dispatch_group(kReadinessGroup, &ignored);
      });
  if (!readiness_started.ok()) {
    timer_->cancel(*liveness_handle_);
    liveness_handle_.reset();
    return ProbeSchedulerStartResult{
        .started = false,
        .fallback_active = false,
        .status = platform_failure(
            std::string("probe scheduler could not arm readiness timer: ") +
                readiness_started.error->detail,
            "health.scheduler.start",
            map_platform_result_code(*readiness_started.error)),
    };
  }

  readiness_handle_ = *readiness_started.value;
  fallback_active_ = false;
  fallback_reason_.clear();
  return ProbeSchedulerStartResult{
      .started = true,
      .fallback_active = false,
      .status = HealthOperationStatus::success(),
  };
}

ProbeSchedulerStopResult ProbeScheduler::stop() {
  if (!running()) {
    return ProbeSchedulerStopResult{
        .stopped = false,
        .fallback_active = fallback_active_,
        .status = HealthOperationStatus::success(),
    };
  }

  const auto liveness_cancelled = timer_->cancel(*liveness_handle_);
  if (!liveness_cancelled.ok()) {
    return ProbeSchedulerStopResult{
        .stopped = false,
        .fallback_active = fallback_active_,
        .status = platform_failure(
            std::string("probe scheduler could not cancel liveness timer: ") +
                liveness_cancelled.error->detail,
            "health.scheduler.stop",
            map_platform_result_code(*liveness_cancelled.error)),
    };
  }

  const auto readiness_cancelled = timer_->cancel(*readiness_handle_);
  if (!readiness_cancelled.ok()) {
    return ProbeSchedulerStopResult{
        .stopped = false,
        .fallback_active = fallback_active_,
        .status = platform_failure(
            std::string("probe scheduler could not cancel readiness timer: ") +
                readiness_cancelled.error->detail,
            "health.scheduler.stop",
            map_platform_result_code(*readiness_cancelled.error)),
    };
  }

  liveness_handle_.reset();
  readiness_handle_.reset();
  tick_callback_ = nullptr;
  fallback_active_ = false;
  fallback_reason_.clear();
  return ProbeSchedulerStopResult{
      .stopped = true,
      .fallback_active = false,
      .status = HealthOperationStatus::success(),
  };
}

ProbeSchedulerTickResult ProbeScheduler::tick_once() {
  if (!config_.is_valid()) {
    return ProbeSchedulerTickResult{
        .triggered = false,
        .fallback_active = fallback_active_,
        .dispatched_groups = {},
        .status = invalid_request(
            "probe scheduler requires a valid health cadence projection before tick_once",
            "health.scheduler.tick_once"),
    };
  }

  if (!config_.enabled) {
    return ProbeSchedulerTickResult{
        .triggered = false,
        .fallback_active = fallback_active_,
        .dispatched_groups = {},
        .status = HealthOperationStatus::success(),
    };
  }

  ProbeSchedulerTickResult result{
      .triggered = true,
      .fallback_active = fallback_active_,
      .dispatched_groups = {},
      .status = HealthOperationStatus::success(),
  };
  dispatch_group(kLivenessGroup, &result.dispatched_groups);
  dispatch_group(kReadinessGroup, &result.dispatched_groups);
  return result;
}

bool ProbeScheduler::running() const {
  return liveness_handle_.has_value() && readiness_handle_.has_value();
}

bool ProbeScheduler::fallback_active() const {
  return fallback_active_;
}

std::string_view ProbeScheduler::fallback_reason() const {
  return fallback_reason_;
}

const HealthResolvedConfig& ProbeScheduler::config() const {
  return config_;
}

HealthOperationStatus ProbeScheduler::invalid_request(std::string message,
                                                      std::string stage) const {
  return HealthOperationStatus::failure(
      contracts::ResultCode::ValidationFieldMissing,
      std::move(message),
      std::move(stage),
      std::string(kProbeSchedulerSourceRef));
}

HealthOperationStatus ProbeScheduler::platform_failure(
    std::string message,
    std::string stage,
    const contracts::ResultCode result_code) const {
  return HealthOperationStatus::failure(result_code,
                                        std::move(message),
                                        std::move(stage),
                                        std::string(kProbeSchedulerSourceRef));
}

void ProbeScheduler::dispatch_group(
    const std::string_view group,
    std::vector<std::string>* const dispatched_groups) {
  dispatched_groups->push_back(std::string(group));
  if (tick_callback_) {
    tick_callback_(group);
  }
}

}  // namespace dasall::infra
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ITimer.h"
#include "health/HealthConfigPolicy.h"

namespace dasall::infra {

struct ProbeSchedulerStartResult {
  bool started = false;
  bool fallback_active = false;
  HealthOperationStatus status = HealthOperationStatus::success();
};

struct ProbeSchedulerStopResult {
  bool stopped = false;
  bool fallback_active = false;
  HealthOperationStatus status = HealthOperationStatus::success();
};

struct ProbeSchedulerTickResult {
  bool triggered = false;
  bool fallback_active = false;
  std::vector<std::string> dispatched_groups;
  HealthOperationStatus status = HealthOperationStatus::success();
};

class ProbeScheduler {
 public:
  using TickCallback = std::function<void(std::string_view group)>;

  explicit ProbeScheduler(HealthResolvedConfig config = {},
                          std::shared_ptr<platform::ITimer> timer = nullptr);

  [[nodiscard]] ProbeSchedulerStartResult start(TickCallback tick_callback);
  [[nodiscard]] ProbeSchedulerStopResult stop();
  [[nodiscard]] ProbeSchedulerTickResult tick_once();

  [[nodiscard]] bool running() const;
  [[nodiscard]] bool fallback_active() const;
  [[nodiscard]] std::string_view fallback_reason() const;
  [[nodiscard]] const HealthResolvedConfig& config() const;

 private:
  [[nodiscard]] HealthOperationStatus invalid_request(std::string message,
                                                      std::string stage) const;
  [[nodiscard]] HealthOperationStatus platform_failure(std::string message,
                                                       std::string stage,
                                                       contracts::ResultCode result_code) const;
  void dispatch_group(std::string_view group,
                      std::vector<std::string>* dispatched_groups);

  HealthResolvedConfig config_;
  std::shared_ptr<platform::ITimer> timer_;
  TickCallback tick_callback_;
  std::optional<platform::TimerHandle> liveness_handle_;
  std::optional<platform::TimerHandle> readiness_handle_;
  bool fallback_active_ = false;
  std::string fallback_reason_;
};

}  // namespace dasall::infra
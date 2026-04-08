#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "watchdog/ITimeoutPolicy.h"
#include "watchdog/IWatchdogService.h"
#include "watchdog/TimeoutDecision.h"

namespace dasall::infra::watchdog {

class TimeoutPolicyEngine final : public ITimeoutPolicy {
 public:
  explicit TimeoutPolicyEngine(WatchdogServiceConfig config = {});

  [[nodiscard]] TimeoutPolicyEvaluationResult evaluate(
      std::shared_ptr<const HeartbeatSample> candidate,
      const TimeoutHistoryWindow& history) const override;

  [[nodiscard]] const WatchdogServiceConfig& config() const;

 private:
  [[nodiscard]] std::uint32_t grace_scan_budget() const;
  [[nodiscard]] static std::uint32_t next_consecutive_miss(
      std::string_view entity_id,
      const TimeoutHistoryWindow& history);
  [[nodiscard]] static std::optional<WatchdogTimeoutLevel> highest_prior_level(
      std::string_view entity_id,
      const TimeoutHistoryWindow& history);
  [[nodiscard]] WatchdogTimeoutLevel determine_level(
      std::uint32_t consecutive_miss,
      std::optional<WatchdogTimeoutLevel> previous_level) const;
  [[nodiscard]] static std::string build_evidence_ref(
      const HeartbeatSample& candidate,
      WatchdogTimeoutLevel level,
      std::uint32_t consecutive_miss,
      const WatchdogServiceConfig& config);

  WatchdogServiceConfig config_;
};

}  // namespace dasall::infra::watchdog
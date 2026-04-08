#include "watchdog/TimeoutPolicyEngine.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "watchdog/HeartbeatSample.h"
#include "watchdog/WatchdogErrors.h"

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kTimeoutPolicyEngineSourceRef = "TimeoutPolicyEngine";

[[nodiscard]] TimeoutPolicyEvaluationResult make_failure(
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return TimeoutPolicyEvaluationResult::failure(
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kTimeoutPolicyEngineSourceRef));
}

}  // namespace

TimeoutPolicyEngine::TimeoutPolicyEngine(WatchdogServiceConfig config)
    : config_(std::move(config)) {}

TimeoutPolicyEvaluationResult TimeoutPolicyEngine::evaluate(
    std::shared_ptr<const HeartbeatSample> candidate,
    const TimeoutHistoryWindow& history) const {
  if (candidate == nullptr || !candidate->has_required_fields()) {
    return make_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "timeout policy requires a non-null candidate with entity_id, timestamps, deadline_ts, and seq_no",
        "watchdog.timeout_policy.evaluate");
  }

  if (!history.has_bindable_inputs()) {
    return make_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "timeout policy requires at least one recent sample or prior decision in the history window",
        "watchdog.timeout_policy.evaluate");
  }

  if (!config_.is_valid()) {
    return make_failure(
        contracts::ResultCode::ValidationFieldMissing,
        "timeout policy engine requires a valid watchdog service config before evaluation",
        "watchdog.timeout_policy.evaluate");
  }

  const auto consecutive_miss =
      next_consecutive_miss(candidate->entity_id, history);
  const auto previous_level = highest_prior_level(candidate->entity_id, history);
  const auto timeout_level = determine_level(consecutive_miss, previous_level);

  auto decision = std::make_shared<TimeoutDecision>();
  decision->entity_id = candidate->entity_id;
  decision->timeout_level = timeout_level;
  decision->consecutive_miss = consecutive_miss;
  decision->reason_code =
      map_watchdog_error_code(WatchdogErrorCode::TimeoutCritical).result_code;
  decision->evidence_ref = build_evidence_ref(
      *candidate,
      timeout_level,
      consecutive_miss,
      config_);

  return TimeoutPolicyEvaluationResult::success(std::move(decision));
}

const WatchdogServiceConfig& TimeoutPolicyEngine::config() const {
  return config_;
}

std::uint32_t TimeoutPolicyEngine::grace_scan_budget() const {
  if (config_.grace_ms == 0U) {
    return 0U;
  }

  const auto numerator =
      static_cast<std::uint64_t>(config_.grace_ms) + config_.scan_interval_ms - 1U;
  return static_cast<std::uint32_t>(numerator / config_.scan_interval_ms);
}

std::uint32_t TimeoutPolicyEngine::next_consecutive_miss(
    std::string_view entity_id,
    const TimeoutHistoryWindow& history) {
  std::uint32_t previous_max = 0U;

  for (const auto& decision : history.prior_decisions) {
    if (decision == nullptr || !decision->has_required_fields() ||
        decision->entity_id != entity_id) {
      continue;
    }

    previous_max = std::max(previous_max, decision->consecutive_miss);
  }

  return previous_max + 1U;
}

std::optional<WatchdogTimeoutLevel> TimeoutPolicyEngine::highest_prior_level(
    std::string_view entity_id,
    const TimeoutHistoryWindow& history) {
  std::optional<WatchdogTimeoutLevel> previous_level;

  for (const auto& decision : history.prior_decisions) {
    if (decision == nullptr || !decision->has_required_fields() ||
        decision->entity_id != entity_id) {
      continue;
    }

    if (!previous_level.has_value() ||
        static_cast<int>(decision->timeout_level) >
            static_cast<int>(*previous_level)) {
      previous_level = decision->timeout_level;
    }
  }

  return previous_level;
}

WatchdogTimeoutLevel TimeoutPolicyEngine::determine_level(
    std::uint32_t consecutive_miss,
    std::optional<WatchdogTimeoutLevel> previous_level) const {
  const auto in_grace_window =
      grace_scan_budget() > 0U && consecutive_miss <= grace_scan_budget();
  const auto prior_critical_or_higher =
      previous_level.has_value() &&
      (previous_level == WatchdogTimeoutLevel::Critical ||
       previous_level == WatchdogTimeoutLevel::Fatal);

  if (config_.timeout_level_policy ==
      WatchdogTimeoutLevelPolicy::WarnThenCritical) {
    if (in_grace_window ||
        consecutive_miss < config_.consecutive_miss_threshold) {
      return WatchdogTimeoutLevel::Warning;
    }

    if (prior_critical_or_higher ||
        consecutive_miss > config_.consecutive_miss_threshold) {
      return WatchdogTimeoutLevel::Fatal;
    }

    return WatchdogTimeoutLevel::Critical;
  }

  if (in_grace_window) {
    return WatchdogTimeoutLevel::Warning;
  }

  if (prior_critical_or_higher ||
      consecutive_miss > config_.consecutive_miss_threshold) {
    return WatchdogTimeoutLevel::Fatal;
  }

  return WatchdogTimeoutLevel::Critical;
}

std::string TimeoutPolicyEngine::build_evidence_ref(
    const HeartbeatSample& candidate,
    WatchdogTimeoutLevel level,
    std::uint32_t consecutive_miss,
    const WatchdogServiceConfig& config) {
  std::ostringstream stream;
  stream << "watchdog://timeout-policy/" << candidate.entity_id
         << "?level=" << watchdog_timeout_level_name(level)
         << "&miss=" << consecutive_miss
         << "&seq=" << candidate.seq_no
         << "&deadline_ts=" << candidate.deadline_ts
         << "&policy="
         << (config.timeout_level_policy ==
                     WatchdogTimeoutLevelPolicy::WarnThenCritical
                 ? "warn_then_critical"
                 : "critical_only")
         << "&threshold=" << config.consecutive_miss_threshold
         << "&grace_ms=" << config.grace_ms;
  return stream.str();
}

}  // namespace dasall::infra::watchdog
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "error/ResultCode.h"

namespace dasall::infra::watchdog {

enum class WatchdogTimeoutLevel {
  Unspecified = 0,
  Warning = 1,
  Critical = 2,
  Fatal = 3,
};

inline constexpr std::string_view watchdog_timeout_level_name(
    WatchdogTimeoutLevel level) {
  switch (level) {
    case WatchdogTimeoutLevel::Unspecified:
      return "unspecified";
    case WatchdogTimeoutLevel::Warning:
      return "warning";
    case WatchdogTimeoutLevel::Critical:
      return "critical";
    case WatchdogTimeoutLevel::Fatal:
      return "fatal";
  }

  return "unspecified";
}

struct TimeoutDecision {
  std::string entity_id;
  WatchdogTimeoutLevel timeout_level = WatchdogTimeoutLevel::Unspecified;
  std::uint32_t consecutive_miss = 0;
  contracts::ResultCode reason_code =
      contracts::ResultCode::RuntimeRetryExhausted;
  std::string evidence_ref;

  [[nodiscard]] bool references_contract_reason_code() const {
    return contracts::classify_result_code(reason_code) !=
           contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool has_required_fields() const {
    return !entity_id.empty() && timeout_level != WatchdogTimeoutLevel::Unspecified &&
           consecutive_miss > 0 && references_contract_reason_code() &&
           !evidence_ref.empty();
  }

  [[nodiscard]] bool is_escalation_of(const TimeoutDecision& previous) const {
    return static_cast<int>(timeout_level) > static_cast<int>(previous.timeout_level) &&
           consecutive_miss >= previous.consecutive_miss;
  }
};

}  // namespace dasall::infra::watchdog
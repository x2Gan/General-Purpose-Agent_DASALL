#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "RuntimeErrorCode.h"
#include "RuntimePolicySnapshot.h"
#include "budget/BudgetDecision.h"
#include "checkpoint/RecoveryOutcome.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::infra::logging {
class ILogger;
}

namespace dasall::runtime {

enum class SafeModeTriggerKind : std::uint8_t {
  PolicyForbidden = 0,
  BudgetExhausted,
  RecoveryExhausted,
  DependencyUnavailable,
  WatchdogTimeout,
};

[[nodiscard]] constexpr const char* safe_mode_trigger_name(const SafeModeTriggerKind trigger) {
  switch (trigger) {
    case SafeModeTriggerKind::PolicyForbidden:
      return "PolicyForbidden";
    case SafeModeTriggerKind::BudgetExhausted:
      return "BudgetExhausted";
    case SafeModeTriggerKind::RecoveryExhausted:
      return "RecoveryExhausted";
    case SafeModeTriggerKind::DependencyUnavailable:
      return "DependencyUnavailable";
    case SafeModeTriggerKind::WatchdogTimeout:
      return "WatchdogTimeout";
  }

  return "Unknown";
}

enum class SafeModeState : std::uint8_t {
  Normal = 0,
  FailedSafe,
  Degraded,
  SafeMode,
};

[[nodiscard]] constexpr const char* safe_mode_state_name(const SafeModeState state) {
  switch (state) {
    case SafeModeState::Normal:
      return "Normal";
    case SafeModeState::FailedSafe:
      return "FailedSafe";
    case SafeModeState::Degraded:
      return "Degraded";
    case SafeModeState::SafeMode:
      return "SafeMode";
  }

  return "Unknown";
}

enum class SafeModeAction : std::uint8_t {
  None = 0,
  EnterFailedSafe,
  EnterDegraded,
  EnterSafeMode,
  ExitToNormal,
};

struct HealthSignal {
  bool dependency_available = true;
  bool watchdog_healthy = true;
  std::string dependency_name;
  std::string detail;
};

struct SafeModeTrigger {
  SafeModeTriggerKind trigger_kind = SafeModeTriggerKind::PolicyForbidden;
  std::optional<BudgetDecision> budget_decision;
  std::optional<contracts::RecoveryOutcome> recovery_outcome;
  std::optional<RuntimeErrorCode> error_code;
  std::optional<HealthSignal> health_signal;
  std::string detail;
};

struct SafeModeDecision {
  bool transition_required = false;
  SafeModeState previous_mode = SafeModeState::Normal;
  SafeModeState target_mode = SafeModeState::Normal;
  SafeModeAction action = SafeModeAction::None;
  std::optional<RuntimeState> target_runtime_state;
  std::optional<RuntimeErrorCode> error_code;
  std::optional<std::string> selected_fallback;
  std::string detail;

  [[nodiscard]] bool mode_changed() const {
    return transition_required && previous_mode != target_mode;
  }
};

struct SafeModeExitRequest {
  bool dependencies_healthy = true;
  bool watchdog_healthy = true;
  bool operator_cleared = true;
  bool budget_restored = true;
  std::string detail;
};

class SafeModeController final {
 public:
  explicit SafeModeController(
      std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot = nullptr);

  void set_logger(
    std::shared_ptr<infra::logging::ILogger> logger,
    std::optional<std::string> runtime_instance_id = std::nullopt);

  [[nodiscard]] SafeModeDecision evaluate_entry(const SafeModeTrigger& trigger);
  [[nodiscard]] SafeModeDecision evaluate_exit(const SafeModeExitRequest& request);
  [[nodiscard]] SafeModeState current_mode() const;

 private:
  [[nodiscard]] std::optional<std::string> select_fallback(
      SafeModeTriggerKind trigger_kind) const;
  [[nodiscard]] SafeModeDecision no_transition(
      const std::string& detail,
      const SafeModeState state) const;
  [[nodiscard]] SafeModeDecision transition_locked(
      SafeModeState target_mode,
      SafeModeAction action,
      std::optional<std::string> selected_fallback,
      std::string detail);

  mutable std::mutex mode_mutex_;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot_;
  SafeModeState current_mode_ = SafeModeState::Normal;
  std::shared_ptr<infra::logging::ILogger> logger_;
  std::optional<std::string> runtime_instance_id_;
};

}  // namespace dasall::runtime
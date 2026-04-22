#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "RuntimeErrorCode.h"
#include "checkpoint/RecoveryOutcome.h"
#include "checkpoint/RecoveryRequest.h"
#include "recovery/ResumePlan.h"

namespace dasall::runtime {

enum class RecoveryAdmission : std::uint8_t {
  Admit = 0,
  Reject,
  Escalate,
};

[[nodiscard]] constexpr const char* recovery_admission_name(
    const RecoveryAdmission admission) {
  switch (admission) {
    case RecoveryAdmission::Admit:
      return "Admit";
    case RecoveryAdmission::Reject:
      return "Reject";
    case RecoveryAdmission::Escalate:
      return "Escalate";
  }

  return "Unknown";
}

struct SafeFailureHint {
  bool enter_safe_mode = false;
  bool enter_degraded_mode = false;
  std::string reason;
};

struct RecoveryExecutionPlan {
  RecoveryAdmission admission = RecoveryAdmission::Reject;
  std::string planned_action;
  std::optional<ResumePlan> resume_plan;
  std::optional<SafeFailureHint> safe_failure_hint;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool executable() const {
    return admission == RecoveryAdmission::Admit;
  }

  [[nodiscard]] bool escalated() const {
    return admission == RecoveryAdmission::Escalate;
  }
};

struct RecoveryApplyResult {
  bool applied = false;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;
};

[[nodiscard]] inline RecoveryExecutionPlan make_recovery_execution_plan(
    const std::string& planned_action,
    const std::string& detail = std::string(),
    const std::optional<ResumePlan>& resume_plan = std::nullopt) {
  return RecoveryExecutionPlan{
      .admission = RecoveryAdmission::Admit,
      .planned_action = planned_action,
      .resume_plan = resume_plan,
      .safe_failure_hint = std::nullopt,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline RecoveryExecutionPlan make_recovery_rejection_plan(
    const std::string& detail,
    const std::optional<RuntimeErrorCode> error_code =
        RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED) {
  return RecoveryExecutionPlan{
      .admission = RecoveryAdmission::Reject,
      .planned_action = "reject",
      .resume_plan = std::nullopt,
      .safe_failure_hint = std::nullopt,
      .error_code = error_code,
      .detail = detail,
  };
}

[[nodiscard]] inline RecoveryExecutionPlan make_recovery_escalation_plan(
    const std::string& detail,
    const SafeFailureHint& safe_failure_hint) {
  return RecoveryExecutionPlan{
      .admission = RecoveryAdmission::Escalate,
      .planned_action = "escalate",
      .resume_plan = std::nullopt,
      .safe_failure_hint = safe_failure_hint,
      .error_code = RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED,
      .detail = detail,
  };
}

class IRecoveryManager {
 public:
  virtual ~IRecoveryManager() = default;

  [[nodiscard]] virtual RecoveryExecutionPlan evaluate(
      const contracts::RecoveryRequest& request) const = 0;

  [[nodiscard]] virtual contracts::RecoveryOutcome execute(
      const RecoveryExecutionPlan& plan) = 0;

  [[nodiscard]] virtual RecoveryApplyResult apply(
      const contracts::RecoveryOutcome& outcome) = 0;
};

}  // namespace dasall::runtime
#pragma once

#include <string_view>

namespace dasall::runtime {

enum class RuntimeErrorDomain {
  Unknown = 0,
  ConfigurationAndInitialization = 1,
  FsmAndStateTransition = 2,
  BudgetAndProtection = 3,
  SessionAndCheckpoint = 4,
  RecoveryAndSafety = 5,
  DownstreamTimeoutAndCommunication = 6,
};

enum class RuntimeErrorCode : int {
  RT_E_000_UNSPECIFIED = 0,

  RT_E_100_CONFIG_MISSING = 100,
  RT_E_101_PROFILE_INVALID = 101,
  RT_E_102_DEPENDENCY_UNAVAILABLE = 102,

  RT_E_200_ILLEGAL_TRANSITION = 200,
  RT_E_201_GUARD_VIOLATED = 201,
  RT_E_202_STATE_INCONSISTENT = 202,

  RT_E_300_BUDGET_EXHAUSTED = 300,
  RT_E_301_TURN_OVERRUN = 301,
  RT_E_302_TOOL_CALL_OVERRUN = 302,
  RT_E_303_LATENCY_OVERRUN = 303,
  RT_E_304_REPLAN_OVERRUN = 304,

  RT_E_400_SESSION_NOT_FOUND = 400,
  RT_E_401_SESSION_INCONSISTENT = 401,
  RT_E_410_CHECKPOINT_CORRUPT = 410,
  RT_E_411_CHECKPOINT_SAVE_FAILED = 411,
  RT_E_412_RESUME_REJECTED = 412,

  RT_E_500_RECOVERY_REJECTED = 500,
  RT_E_501_RECOVERY_ESCALATED = 501,
  RT_E_510_SAFE_MODE_ENTERED = 510,
  RT_E_511_DEGRADE_ENTERED = 511,

  RT_E_600_LLM_TIMEOUT = 600,
  RT_E_601_TOOL_TIMEOUT = 601,
  RT_E_602_MEMORY_TIMEOUT = 602,
  RT_E_603_KNOWLEDGE_TIMEOUT = 603,
};

inline constexpr std::string_view runtime_error_domain_name(RuntimeErrorDomain domain) {
  switch (domain) {
    case RuntimeErrorDomain::ConfigurationAndInitialization:
      return "configuration_and_initialization";
    case RuntimeErrorDomain::FsmAndStateTransition:
      return "fsm_and_state_transition";
    case RuntimeErrorDomain::BudgetAndProtection:
      return "budget_and_protection";
    case RuntimeErrorDomain::SessionAndCheckpoint:
      return "session_and_checkpoint";
    case RuntimeErrorDomain::RecoveryAndSafety:
      return "recovery_and_safety";
    case RuntimeErrorDomain::DownstreamTimeoutAndCommunication:
      return "downstream_timeout_and_communication";
    case RuntimeErrorDomain::Unknown:
      return "unknown";
  }

  return "unknown";
}

inline constexpr RuntimeErrorDomain classify_runtime_error_code_value(int raw_code) {
  if (raw_code >= 100 && raw_code <= 199) {
    return RuntimeErrorDomain::ConfigurationAndInitialization;
  }

  if (raw_code >= 200 && raw_code <= 299) {
    return RuntimeErrorDomain::FsmAndStateTransition;
  }

  if (raw_code >= 300 && raw_code <= 399) {
    return RuntimeErrorDomain::BudgetAndProtection;
  }

  if (raw_code >= 400 && raw_code <= 499) {
    return RuntimeErrorDomain::SessionAndCheckpoint;
  }

  if (raw_code >= 500 && raw_code <= 599) {
    return RuntimeErrorDomain::RecoveryAndSafety;
  }

  if (raw_code >= 600 && raw_code <= 699) {
    return RuntimeErrorDomain::DownstreamTimeoutAndCommunication;
  }

  return RuntimeErrorDomain::Unknown;
}

inline constexpr RuntimeErrorDomain classify_runtime_error_code(RuntimeErrorCode code) {
  return classify_runtime_error_code_value(static_cast<int>(code));
}

inline constexpr bool is_known_runtime_error_code(int raw_code) {
  return classify_runtime_error_code_value(raw_code) != RuntimeErrorDomain::Unknown;
}

}  // namespace dasall::runtime
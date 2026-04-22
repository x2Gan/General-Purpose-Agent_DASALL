#include <array>
#include <exception>
#include <iostream>
#include <set>
#include <string>

#include "RuntimeErrorCode.h"
#include "support/TestAssertions.h"

int main() {
  using dasall::runtime::RuntimeErrorCode;
  using dasall::runtime::RuntimeErrorDomain;
  using dasall::runtime::classify_runtime_error_code;
  using dasall::runtime::classify_runtime_error_code_value;
  using dasall::runtime::is_known_runtime_error_code;
  using dasall::runtime::runtime_error_domain_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    constexpr std::array<RuntimeErrorCode, 24> kKnownCodes = {
        RuntimeErrorCode::RT_E_100_CONFIG_MISSING,
        RuntimeErrorCode::RT_E_101_PROFILE_INVALID,
        RuntimeErrorCode::RT_E_102_DEPENDENCY_UNAVAILABLE,
        RuntimeErrorCode::RT_E_200_ILLEGAL_TRANSITION,
        RuntimeErrorCode::RT_E_201_GUARD_VIOLATED,
        RuntimeErrorCode::RT_E_202_STATE_INCONSISTENT,
        RuntimeErrorCode::RT_E_300_BUDGET_EXHAUSTED,
        RuntimeErrorCode::RT_E_301_TURN_OVERRUN,
        RuntimeErrorCode::RT_E_302_TOOL_CALL_OVERRUN,
        RuntimeErrorCode::RT_E_303_LATENCY_OVERRUN,
        RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN,
        RuntimeErrorCode::RT_E_400_SESSION_NOT_FOUND,
        RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT,
        RuntimeErrorCode::RT_E_411_CHECKPOINT_SAVE_FAILED,
        RuntimeErrorCode::RT_E_412_RESUME_REJECTED,
        RuntimeErrorCode::RT_E_500_RECOVERY_REJECTED,
        RuntimeErrorCode::RT_E_501_RECOVERY_ESCALATED,
        RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
        RuntimeErrorCode::RT_E_511_DEGRADE_ENTERED,
        RuntimeErrorCode::RT_E_600_LLM_TIMEOUT,
        RuntimeErrorCode::RT_E_601_TOOL_TIMEOUT,
        RuntimeErrorCode::RT_E_602_MEMORY_TIMEOUT,
        RuntimeErrorCode::RT_E_603_KNOWLEDGE_TIMEOUT,
    };

    std::set<int> unique_codes;
    for (const auto code : kKnownCodes) {
      unique_codes.insert(static_cast<int>(code));
    }

    assert_equal(static_cast<int>(kKnownCodes.size()), static_cast<int>(unique_codes.size()),
                 "runtime error code values must be unique");
    assert_equal(100, static_cast<int>(RuntimeErrorCode::RT_E_100_CONFIG_MISSING),
                 "config missing code mismatch");
    assert_equal(603, static_cast<int>(RuntimeErrorCode::RT_E_603_KNOWLEDGE_TIMEOUT),
                 "knowledge timeout code mismatch");

    assert_true(classify_runtime_error_code(RuntimeErrorCode::RT_E_100_CONFIG_MISSING) ==
                    RuntimeErrorDomain::ConfigurationAndInitialization,
                "1xx codes should map to configuration/init domain");
    assert_true(classify_runtime_error_code(RuntimeErrorCode::RT_E_201_GUARD_VIOLATED) ==
                    RuntimeErrorDomain::FsmAndStateTransition,
                "2xx codes should map to FSM domain");
    assert_true(classify_runtime_error_code(RuntimeErrorCode::RT_E_303_LATENCY_OVERRUN) ==
                    RuntimeErrorDomain::BudgetAndProtection,
                "3xx codes should map to budget domain");
    assert_true(classify_runtime_error_code(RuntimeErrorCode::RT_E_412_RESUME_REJECTED) ==
                    RuntimeErrorDomain::SessionAndCheckpoint,
                "4xx codes should map to session/checkpoint domain");
    assert_true(classify_runtime_error_code(RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED) ==
                    RuntimeErrorDomain::RecoveryAndSafety,
                "5xx codes should map to recovery/safety domain");
    assert_true(classify_runtime_error_code(RuntimeErrorCode::RT_E_601_TOOL_TIMEOUT) ==
                    RuntimeErrorDomain::DownstreamTimeoutAndCommunication,
                "6xx codes should map to downstream timeout domain");

    assert_equal("recovery_and_safety", std::string(runtime_error_domain_name(
                                               RuntimeErrorDomain::RecoveryAndSafety)),
                 "runtime error domain name mismatch");
    assert_true(classify_runtime_error_code_value(99) == RuntimeErrorDomain::Unknown,
                "codes below RT_E_1xx should be rejected");
    assert_true(classify_runtime_error_code_value(700) == RuntimeErrorDomain::Unknown,
                "codes above RT_E_6xx should be rejected");
    assert_true(!is_known_runtime_error_code(99),
                "unknown runtime error code must not be accepted");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
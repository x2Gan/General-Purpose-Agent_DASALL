#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "watchdog/WatchdogErrors.h"
#include "support/TestAssertions.h"

namespace {

struct WatchdogErrorMappingExpectation {
  dasall::infra::watchdog::WatchdogErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
  std::string_view source_anchor;
};

void test_watchdog_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::watchdog::map_watchdog_error_code;
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchdogErrorMapping;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(WatchdogErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<WatchdogErrorMappingExpectation, 7> kFrozenMappings{ {
      {WatchdogErrorCode::EntityDuplicate,
       "INF_E_WATCHDOG_ENTITY_DUPLICATE",
       ResultCode::ValidationFieldMissing,
       "6.6 entity duplicate"},
      {WatchdogErrorCode::EntityNotFound,
       "INF_E_WATCHDOG_ENTITY_NOT_FOUND",
       ResultCode::ValidationFieldMissing,
       "6.6 entity lookup"},
      {WatchdogErrorCode::HeartbeatStale,
       "INF_E_WATCHDOG_HEARTBEAT_STALE",
       ResultCode::ValidationFieldMissing,
       "6.3 heartbeat stale"},
      {WatchdogErrorCode::ScanOverdue,
       "INF_E_WATCHDOG_SCAN_OVERDUE",
       ResultCode::RuntimeRetryExhausted,
       "6.8 scan overdue"},
      {WatchdogErrorCode::TimeoutCritical,
       "INF_E_WATCHDOG_TIMEOUT_CRITICAL",
       ResultCode::ProviderTimeout,
       "6.8 timeout critical"},
      {WatchdogErrorCode::EventPublishFail,
       "INF_E_WATCHDOG_EVENT_PUBLISH_FAIL",
       ResultCode::ToolExecutionFailed,
       "6.8 event publish failure"},
      {WatchdogErrorCode::AuditWriteFail,
       "INF_E_WATCHDOG_AUDIT_WRITE_FAIL",
       ResultCode::RuntimeRetryExhausted,
       "6.8 audit write failure"},
  } };

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_watchdog_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("watchdog error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.name),
                 std::string(watchdog_error_code_name(expectation.code)),
                 std::string("watchdog error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.source_anchor),
                 std::string(mapping.source_anchor),
                 std::string("watchdog error source anchor should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each watchdog private error mapping should carry a non-empty reason");
  }
}

void test_watchdog_error_names_stay_local_to_watchdog_boundary() {
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::tests::support::assert_true;

  constexpr std::array<WatchdogErrorCode, 7> kFrozenCodes{
      WatchdogErrorCode::EntityDuplicate,
      WatchdogErrorCode::EntityNotFound,
      WatchdogErrorCode::HeartbeatStale,
      WatchdogErrorCode::ScanOverdue,
      WatchdogErrorCode::TimeoutCritical,
      WatchdogErrorCode::EventPublishFail,
      WatchdogErrorCode::AuditWriteFail,
  };

  for (const auto code : kFrozenCodes) {
    assert_true(watchdog_error_code_name(code).starts_with("INF_E_WATCHDOG_"),
                "watchdog private error names should remain inside the INF_E_WATCHDOG_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_watchdog_error_mapping_matrix_stays_frozen();
    test_watchdog_error_names_stay_local_to_watchdog_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
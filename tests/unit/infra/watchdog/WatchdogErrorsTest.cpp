#include <array>
#include <exception>
#include <iostream>

#include "watchdog/WatchdogErrors.h"
#include "support/TestAssertions.h"

namespace {

void test_watchdog_error_code_names_and_ordinals_are_stable() {
  using dasall::infra::watchdog::watchdog_error_code_name;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::tests::support::assert_equal;

  assert_equal(1,
               static_cast<int>(WatchdogErrorCode::EntityDuplicate),
               "watchdog entity duplicate ordinal should remain stable");
  assert_equal(2,
               static_cast<int>(WatchdogErrorCode::EntityNotFound),
               "watchdog entity not found ordinal should remain stable");
  assert_equal(3,
               static_cast<int>(WatchdogErrorCode::HeartbeatStale),
               "watchdog stale heartbeat ordinal should remain stable");
  assert_equal(4,
               static_cast<int>(WatchdogErrorCode::ScanOverdue),
               "watchdog scan overdue ordinal should remain stable");
  assert_equal(5,
               static_cast<int>(WatchdogErrorCode::TimeoutCritical),
               "watchdog timeout critical ordinal should remain stable");
  assert_equal(6,
               static_cast<int>(WatchdogErrorCode::EventPublishFail),
               "watchdog event publish fail ordinal should remain stable");
  assert_equal(7,
               static_cast<int>(WatchdogErrorCode::AuditWriteFail),
               "watchdog audit write fail ordinal should remain stable");

  assert_equal(std::string("INF_E_WATCHDOG_ENTITY_DUPLICATE"),
               std::string(watchdog_error_code_name(WatchdogErrorCode::EntityDuplicate)),
               "watchdog entity duplicate error name should remain stable");
  assert_equal(std::string("INF_E_WATCHDOG_AUDIT_WRITE_FAIL"),
               std::string(watchdog_error_code_name(WatchdogErrorCode::AuditWriteFail)),
               "watchdog audit write fail error name should remain stable");
}

void test_watchdog_error_mapping_stays_inside_contract_categories() {
  using dasall::contracts::classify_result_code;
  using dasall::contracts::ResultCodeCategory;
  using dasall::infra::watchdog::map_watchdog_error_code;
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
    const auto mapping = map_watchdog_error_code(code);
    const auto category = classify_result_code(mapping.result_code);

    assert_true(category == ResultCodeCategory::Validation ||
                    category == ResultCodeCategory::Tool ||
                    category == ResultCodeCategory::Provider ||
                    category == ResultCodeCategory::Runtime,
                "watchdog private error mapping should remain inside the frozen contracts categories");
    assert_true(!mapping.reason.empty() && !mapping.source_anchor.empty(),
                "watchdog private error mapping should expose observable source anchors and reasons");
  }
}

}  // namespace

int main() {
  try {
    test_watchdog_error_code_names_and_ordinals_are_stable();
    test_watchdog_error_mapping_stays_inside_contract_categories();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
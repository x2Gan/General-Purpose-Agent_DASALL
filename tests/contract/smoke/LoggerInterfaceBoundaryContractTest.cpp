#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/ILogger.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_logger_interface_result_uses_contract_error_types_only() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::LogWriteResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LogWriteResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(LogWriteResult{}.error), std::optional<ErrorInfo>>);

  const auto failure = LogWriteResult::failure(ResultCode::ValidationFieldMissing,
                                               "attrs must be serializable",
                                               "logging.log",
                                               "ILogger");

  assert_true(!failure.ok,
              "logger boundary failures should remain explicit failures");
  assert_true(failure.references_only_contract_error_types(),
              "ILogger should expose only contracts ResultCode/ErrorInfo types");
}

void test_logger_interface_keeps_flush_deadline_as_local_placeholder_type() {
  using dasall::infra::LogFlushDeadline;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LogFlushDeadline{}.timeout_ms), std::uint32_t>);

  const LogFlushDeadline valid_deadline{.timeout_ms = 250};
  const LogFlushDeadline invalid_deadline{};

  assert_true(valid_deadline.is_valid(),
              "positive timeout values should satisfy the placeholder flush deadline guard");
  assert_true(!invalid_deadline.is_valid(),
              "zero timeout should remain invalid until deadline semantics are fully designed");
}

}  // namespace

int main() {
  try {
    test_logger_interface_result_uses_contract_error_types_only();
    test_logger_interface_keeps_flush_deadline_as_local_placeholder_type();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
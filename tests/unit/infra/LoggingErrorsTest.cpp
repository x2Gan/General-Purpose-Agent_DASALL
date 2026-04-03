#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "logging/LoggingErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct LoggingErrorExpectation {
  dasall::infra::logging::LoggingErrorCode code;
  int raw_value;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
  std::string_view source_anchor;
};

void test_logging_error_code_values_names_and_mappings_are_stable() {
  using dasall::infra::logging::logging_error_code_name;
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingErrorMapping;
  using dasall::infra::logging::map_logging_error_code;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;
  using ResultCode = dasall::contracts::ResultCode;

  static_assert(std::is_same_v<decltype(LoggingErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<LoggingErrorExpectation, 4> kFrozenMappings{ {
      {LoggingErrorCode::QueueFull,
       1,
       "LOG_E_QUEUE_FULL",
       ResultCode::RuntimeRetryExhausted,
       "6.8 queue full"},
      {LoggingErrorCode::SinkIo,
       2,
       "LOG_E_SINK_IO",
       ResultCode::ProviderTimeout,
       "6.8 sink IO failure"},
      {LoggingErrorCode::FormatInvalid,
       3,
       "LOG_E_FORMAT_INVALID",
       ResultCode::ValidationFieldMissing,
       "6.8 format failure"},
      {LoggingErrorCode::ConfigInvalid,
       4,
       "LOG_E_CONFIG_INVALID",
       ResultCode::ValidationFieldMissing,
       "6.6 ILogConfigurator"},
  } };

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_logging_error_code(expectation.code);
    assert_equal(expectation.raw_value,
                 static_cast<int>(expectation.code),
                 "logging error enum numeric values should remain stable");
    assert_equal(std::string(expectation.name),
                 std::string(logging_error_code_name(expectation.code)),
                 std::string("logging error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("logging error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.source_anchor),
                 std::string(mapping.source_anchor),
                 std::string("logging error source anchor should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each logging private error mapping should carry a non-empty observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_logging_error_code_values_names_and_mappings_are_stable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "logging/LoggingErrors.h"
#include "support/TestAssertions.h"

namespace {

void test_logging_error_codes_map_only_to_existing_contract_result_codes() {
  using ResultCode = dasall::contracts::ResultCode;
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingErrorMapping;
  using dasall::infra::logging::map_logging_error_code;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(LoggingErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<LoggingErrorCode, 4> kFrozenCodes{
      LoggingErrorCode::QueueFull,
      LoggingErrorCode::SinkIo,
      LoggingErrorCode::FormatInvalid,
      LoggingErrorCode::ConfigInvalid,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_logging_error_code(code);
    assert_true(mapping.result_code == ResultCode::ValidationFieldMissing ||
                    mapping.result_code == ResultCode::ProviderTimeout ||
                    mapping.result_code == ResultCode::RuntimeRetryExhausted,
                "logging private errors should map only to existing contracts result codes");
  }
}

void test_logging_error_code_names_stay_private_to_logging_boundary() {
  using dasall::infra::logging::logging_error_code_name;
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::tests::support::assert_true;

  constexpr std::array<LoggingErrorCode, 4> kFrozenCodes{
      LoggingErrorCode::QueueFull,
      LoggingErrorCode::SinkIo,
      LoggingErrorCode::FormatInvalid,
      LoggingErrorCode::ConfigInvalid,
  };

  for (const auto code : kFrozenCodes) {
    const auto name = logging_error_code_name(code);
    assert_true(name.starts_with("LOG_E_"),
                "logging private error names should remain inside the LOG_E_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_logging_error_codes_map_only_to_existing_contract_result_codes();
    test_logging_error_code_names_stay_private_to_logging_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
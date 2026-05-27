#include <cstdint>
#include <exception>
#include <iostream>

#include "logging/LoggingConfigAdapter.h"
#include "support/TestAssertions.h"

namespace {

void test_logging_config_adapter_parse_uint32_accepts_clean_positive_values() {
  using dasall::infra::logging::LoggingConfigAdapter;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  std::uint32_t parsed_value = 0;
  assert_true(LoggingConfigAdapter::parse_uint32_value("16384", parsed_value),
              "logging config adapter should accept clean positive uint32 values");
  assert_equal(16384, static_cast<int>(parsed_value),
               "logging config adapter should preserve the parsed queue size value");
}

void test_logging_config_adapter_parse_uint32_rejects_trailing_junk() {
  using dasall::infra::logging::LoggingConfigAdapter;
  using dasall::tests::support::assert_true;

  std::uint32_t parsed_value = 0;
  assert_true(!LoggingConfigAdapter::parse_uint32_value("8192junk", parsed_value),
              "logging config adapter should reject unsigned integers with trailing junk");
  assert_true(!LoggingConfigAdapter::parse_uint32_value("50MB", parsed_value),
              "logging config adapter should reject rotation values with trailing suffix text");
}

}  // namespace

int main() {
  try {
    test_logging_config_adapter_parse_uint32_accepts_clean_positive_values();
    test_logging_config_adapter_parse_uint32_rejects_trailing_junk();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
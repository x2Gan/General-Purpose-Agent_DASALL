#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"
#include "validation/ToolValidator.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;
using dasall::tools::validation::ToolValidator;

void test_empty_arguments_returns_empty() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("");
  assert_true(result.empty(),
              "normalize_arguments should return empty string for empty input");
}

void test_whitespace_only_arguments_returns_empty() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("   \t\n  ");
  assert_true(result.empty(),
              "normalize_arguments should return empty string for whitespace-only input");
}

void test_valid_json_object_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("{\"key\":\"value\"}");
  assert_equal(std::string("{\"key\":\"value\"}"), result,
               "normalize_arguments should pass through valid JSON object");
}

void test_valid_json_array_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("[1,2,3]");
  assert_equal(std::string("[1,2,3]"), result,
               "normalize_arguments should pass through valid JSON array");
}

void test_valid_json_string_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("\"hello\"");
  assert_equal(std::string("\"hello\""), result,
               "normalize_arguments should pass through JSON string primitive");
}

void test_valid_json_number_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("42");
  assert_equal(std::string("42"), result,
               "normalize_arguments should pass through JSON number primitive");
}

void test_valid_json_negative_number_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("-3.14");
  assert_equal(std::string("-3.14"), result,
               "normalize_arguments should pass through negative JSON number");
}

void test_valid_json_boolean_true_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("true");
  assert_equal(std::string("true"), result,
               "normalize_arguments should pass through JSON boolean true");
}

void test_valid_json_boolean_false_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("false");
  assert_equal(std::string("false"), result,
               "normalize_arguments should pass through JSON boolean false");
}

void test_valid_json_null_passes() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("null");
  assert_equal(std::string("null"), result,
               "normalize_arguments should pass through JSON null");
}

void test_non_json_payload_rejected() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("SELECT * FROM users");
  assert_true(result.empty(),
              "normalize_arguments should reject non-JSON payload starting with S");
}

void test_xml_payload_rejected() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("<root>data</root>");
  assert_true(result.empty(),
              "normalize_arguments should reject XML-like payload starting with <");
}

void test_leading_whitespace_trimmed_then_validated() {
  ToolValidator validator;
  const auto result = validator.normalize_arguments("  {\"key\":\"value\"}  ");
  assert_equal(std::string("{\"key\":\"value\"}"), result,
               "normalize_arguments should trim whitespace then validate JSON structure");
}

void test_overlong_payload_passes_if_valid_json() {
  ToolValidator validator;
  std::string large_payload = "{\"data\":\"" + std::string(8192, 'x') + "\"}";
  const auto result = validator.normalize_arguments(large_payload);
  assert_true(!result.empty(),
              "normalize_arguments should accept large but structurally valid JSON");
}

}  // namespace

int main() {
  try {
    test_empty_arguments_returns_empty();
    test_whitespace_only_arguments_returns_empty();
    test_valid_json_object_passes();
    test_valid_json_array_passes();
    test_valid_json_string_passes();
    test_valid_json_number_passes();
    test_valid_json_negative_number_passes();
    test_valid_json_boolean_true_passes();
    test_valid_json_boolean_false_passes();
    test_valid_json_null_passes();
    test_non_json_payload_rejected();
    test_xml_payload_rejected();
    test_leading_whitespace_trimmed_then_validated();
    test_overlong_payload_passes_if_valid_json();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

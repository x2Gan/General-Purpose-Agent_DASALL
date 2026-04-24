#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

// 包含被测 CLI 组件头文件（通过 apps/cli/src include 路径）
// CMakeLists.txt 中需将 apps/cli/src 加入该测试目标的 include 路径
#include "CliOutputFormatter.h"

namespace {

/// 验证 format_ping_success 包含 "ok"
void test_format_ping_success_contains_ok() {
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto msg = CliOutputFormatter::format_ping_success("");
  assert_true(msg.find("ok") != std::string::npos,
              "format_ping_success should contain 'ok'");
}

/// 验证 format_ping_success 包含 raw_response 内容
void test_format_ping_success_includes_response() {
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto msg = CliOutputFormatter::format_ping_success(
      R"({"status":"ok"})");
  assert_true(msg.find("status") != std::string::npos,
              "format_ping_success should embed raw_response");
}

/// 验证 format_ping_failure 包含 "FAILED"
void test_format_ping_failure_contains_failed() {
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto msg = CliOutputFormatter::format_ping_failure();
  assert_true(msg.find("FAILED") != std::string::npos,
              "format_ping_failure should contain 'FAILED'");
}

/// 验证 format_submit_success 包含 "accept"
void test_format_submit_success_contains_accept() {
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto msg = CliOutputFormatter::format_submit_success("");
  assert_true(msg.find("accept") != std::string::npos,
              "format_submit_success should contain 'accept'");
}

/// 验证 format_error 包含 reason 字符串
void test_format_error_includes_reason() {
  using dasall::apps::cli::CliOutputFormatter;
  using dasall::tests::support::assert_true;

  const auto msg = CliOutputFormatter::format_error("invalid arguments");
  assert_true(msg.find("invalid arguments") != std::string::npos,
              "format_error should include the reason string");
}

}  // namespace

int main() {
  try {
    test_format_ping_success_contains_ok();
    test_format_ping_success_includes_response();
    test_format_ping_failure_contains_failed();
    test_format_submit_success_contains_accept();
    test_format_error_includes_reason();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

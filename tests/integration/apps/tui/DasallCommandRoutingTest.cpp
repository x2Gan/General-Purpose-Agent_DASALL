#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "CliBinaryTestSupport.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_BINARY_PATH
#error "DASALL_TUI_BINARY_PATH must be defined"
#endif

#ifndef DASALL_CLI_BINARY_PATH
#error "DASALL_CLI_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::tests::integration::access_support::ProcessResult;
using dasall::tests::integration::access_support::run_process_capture_split;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] const fs::path& repository_root() {
  static const fs::path root{DASALL_REPOSITORY_ROOT};
  return root;
}

[[nodiscard]] ProcessResult run_tui(const std::initializer_list<std::string> args) {
  std::vector<std::string> argv;
  argv.reserve(args.size() + 1);
  argv.emplace_back(DASALL_TUI_BINARY_PATH);
  argv.insert(argv.end(), args.begin(), args.end());
  return run_process_capture_split(argv, repository_root());
}

[[nodiscard]] ProcessResult run_cli(const std::initializer_list<std::string> args) {
  std::vector<std::string> argv;
  argv.reserve(args.size() + 1);
  argv.emplace_back(DASALL_CLI_BINARY_PATH);
  argv.insert(argv.end(), args.begin(), args.end());
  return run_process_capture_split(argv, repository_root());
}

void test_bare_dasall_non_interactive_startup_fails_closed() {
  const ProcessResult result = run_tui({});

  assert_equal(1,
               result.exit_code,
               "bare dasall should fail closed outside a TTY so routing stays on the TUI entrypoint; stdout=" +
                   result.stdout_text + " stderr=" + result.stderr_text);
  assert_true(
      result.stdout_text.find("TUI startup blocked: stdout is not attached to a TTY.") !=
          std::string::npos,
      "bare dasall should surface the TUI non-TTY startup issue on stdout; stdout=" +
          result.stdout_text);
  assert_true(
      result.stdout_text.find("Use dasall-cli for non-interactive control-plane tasks.") !=
          std::string::npos,
      "bare dasall should redirect non-interactive control-plane use to dasall-cli; stdout=" +
          result.stdout_text);
  assert_true(result.stderr_text.empty(),
              "bare dasall non-TTY redirect should not emit an additional stderr error; stderr=" +
                  result.stderr_text);
}

void test_legacy_bare_status_subcommand_fails_closed_with_migration_hint() {
  const ProcessResult result = run_tui({"status"});

  assert_equal(1,
               result.exit_code,
               "legacy bare dasall status should be rejected after command ownership split; stdout=" +
                   result.stdout_text + " stderr=" + result.stderr_text);
  assert_true(result.stdout_text.empty(),
              "legacy bare dasall status should not fall through to the TUI startup path; stdout=" +
                  result.stdout_text);
  assert_true(
      result.stderr_text.find("bare 'dasall status' is no longer supported.") !=
          std::string::npos,
      "legacy bare dasall status should emit an explicit migration error; stderr=" +
          result.stderr_text);
  assert_true(
      result.stderr_text.find("Use 'dasall-cli status' for structured control-plane tasks.") !=
          std::string::npos,
      "legacy bare dasall status should redirect operators to the structured CLI equivalent; stderr=" +
          result.stderr_text);
}

void test_dasall_cli_status_help_remains_available() {
  const ProcessResult result = run_cli({"status", "--help"});

  assert_equal(0,
               result.exit_code,
               "dasall-cli should keep the structured status command after the TUI takes bare dasall; stdout=" +
                   result.stdout_text + " stderr=" + result.stderr_text);
  assert_true(result.stdout_text.find("Usage: dasall-cli status") != std::string::npos,
              "dasall-cli status --help should keep the structured control-plane usage string; stdout=" +
                  result.stdout_text);
  assert_true(result.stderr_text.empty(),
              "dasall-cli status --help should not emit stderr on the success path; stderr=" +
                  result.stderr_text);
}

}  // namespace

int main() {
  try {
    test_bare_dasall_non_interactive_startup_fails_closed();
    test_legacy_bare_status_subcommand_fails_closed_with_migration_hint();
    test_dasall_cli_status_help_remains_available();
  } catch (const std::exception& exception) {
    std::cerr << "DasallCommandRoutingTest failed: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}
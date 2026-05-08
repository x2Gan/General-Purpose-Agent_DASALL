#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

#include "CliBinaryTestSupport.h"
#include "support/TestAssertions.h"

#ifndef DASALL_CLI_BINARY_PATH
#error "DASALL_CLI_BINARY_PATH must be defined"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#error "DASALL_REPOSITORY_ROOT must be defined"
#endif

namespace {

using dasall::tests::integration::access_support::run_process_capture_split;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void test_fresh_install_entrypoint_is_discoverable_via_help() {
  namespace fs = std::filesystem;

  const fs::path cli_binary = DASALL_CLI_BINARY_PATH;
  const fs::path repository_root = DASALL_REPOSITORY_ROOT;
  assert_true(fs::exists(cli_binary),
              "config integration topology should build the dasall CLI binary before help smoke runs");

  const auto result = run_process_capture_split(
      {cli_binary.string(), "config", "--help"}, repository_root);

  assert_equal(result.exit_code, 0,
               "config integration topology smoke should expose config help without daemon or install-state prerequisites");
  assert_true(result.stdout_text.find("dasall-cli config") != std::string::npos,
              "config integration topology smoke should print the config command family help text");
  assert_true(result.stdout_text.find("config show") != std::string::npos &&
                  result.stdout_text.find("config plan") != std::string::npos &&
                  result.stdout_text.find("config validate") != std::string::npos,
              "config integration topology smoke should list the non-interactive config entrypoints");
}

}  // namespace

int main() {
  try {
    test_fresh_install_entrypoint_is_discoverable_via_help();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigFreshInstallWorkflowTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigFreshInstallWorkflowTest passed\n";
  return 0;
}
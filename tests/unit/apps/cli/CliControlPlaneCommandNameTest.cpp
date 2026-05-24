#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include <unistd.h>

#include "support/TestAssertions.h"

#ifndef DASALL_CLI_BINARY_PATH
#define DASALL_CLI_BINARY_PATH "/home/gangan/DASALL/build/vscode-linux-ninja/apps/cli/dasall-cli"
#endif

#ifndef DASALL_CLI_LEGACY_BINARY_PATH
#define DASALL_CLI_LEGACY_BINARY_PATH "/home/gangan/DASALL/build/vscode-linux-ninja/apps/cli/dasall"
#endif

namespace {

void test_cli_target_outputs_dasall_cli_binary() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path cli_binary{DASALL_CLI_BINARY_PATH};

  assert_equal(std::string("dasall-cli"), cli_binary.filename().string(),
               "dasall-cli target should expose dasall-cli as its build-tree filename");
  assert_true(fs::is_regular_file(cli_binary),
              "dasall-cli target file should exist as a regular file");
  assert_true(::access(cli_binary.c_str(), X_OK) == 0,
              "dasall-cli target file should be executable");
}

void test_legacy_bare_cli_artifact_is_not_produced() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path legacy_binary{DASALL_CLI_LEGACY_BINARY_PATH};

  assert_true(!fs::exists(legacy_binary),
              "structured CLI should no longer produce the legacy bare dasall artifact");
}

}  // namespace

int main() {
  try {
    test_cli_target_outputs_dasall_cli_binary();
    test_legacy_bare_cli_artifact_is_not_produced();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
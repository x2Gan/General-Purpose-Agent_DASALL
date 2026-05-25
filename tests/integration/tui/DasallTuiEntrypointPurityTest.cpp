#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <unistd.h>

#include "CliBinaryTestSupport.h"
#include "support/TestAssertions.h"

#ifndef DASALL_TUI_BINARY_PATH
#define DASALL_TUI_BINARY_PATH "/home/gangan/DASALL/build/vscode-linux-ninja/apps/tui/dasall"
#endif

#ifndef DASALL_TUI_PROTOTYPE_BINARY_PATH
#define DASALL_TUI_PROTOTYPE_BINARY_PATH "/home/gangan/DASALL/build/vscode-linux-ninja/apps/tui/dasall_tui_prototype"
#endif

#ifndef DASALL_APPS_TUI_CMAKE
#define DASALL_APPS_TUI_CMAKE "/home/gangan/DASALL/apps/tui/CMakeLists.txt"
#endif

#ifndef DASALL_REPOSITORY_ROOT
#define DASALL_REPOSITORY_ROOT "/home/gangan/DASALL"
#endif

namespace {

namespace fs = std::filesystem;

constexpr char kNmBinaryPath[] = "/usr/bin/nm";

using dasall::tests::integration::access_support::ProcessResult;
using dasall::tests::integration::access_support::run_process_capture_split;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] const fs::path& repository_root() {
  static const fs::path root{DASALL_REPOSITORY_ROOT};
  return root;
}

[[nodiscard]] ProcessResult run_nm_on_binary(const fs::path& binary_path) {
  return run_process_capture_split({kNmBinaryPath, "-C", binary_path.string()},
                                   repository_root());
}

void formal_binary_excludes_fake_tui_symbols() {
  const fs::path formal_binary{DASALL_TUI_BINARY_PATH};

  assert_true(fs::is_regular_file(formal_binary),
              "formal bare dasall binary should exist before purity audit");
  assert_true(::access(formal_binary.c_str(), X_OK) == 0,
              "formal bare dasall binary should stay executable before purity audit");

  const ProcessResult result = run_nm_on_binary(formal_binary);

  assert_equal(0,
               result.exit_code,
               "nm should succeed when auditing the formal bare dasall binary; stderr=" +
                   result.stderr_text);
  assert_true(result.stdout_text.find("FakeTuiDataSource") == std::string::npos,
              "formal bare dasall binary must not retain FakeTuiDataSource symbols; stdout=" +
                  result.stdout_text);
  assert_true(result.stdout_text.find("FakeScenario") == std::string::npos,
              "formal bare dasall binary must not retain fake scenario symbols; stdout=" +
                  result.stdout_text);
}

void prototype_binary_still_retains_fake_tui_symbols() {
  const fs::path prototype_binary{DASALL_TUI_PROTOTYPE_BINARY_PATH};

  assert_true(fs::is_regular_file(prototype_binary),
              "prototype binary should still build while purity gate strips fake symbols from formal dasall");
  assert_true(::access(prototype_binary.c_str(), X_OK) == 0,
              "prototype binary should stay executable after the core split");

  const ProcessResult result = run_nm_on_binary(prototype_binary);

  assert_equal(0,
               result.exit_code,
               "nm should succeed when auditing the prototype binary; stderr=" +
                   result.stderr_text);
  assert_true(result.stdout_text.find("FakeTuiDataSource") != std::string::npos,
              "prototype binary should retain FakeTuiDataSource symbols after the split; stdout=" +
                  result.stdout_text);
}

void cmake_wires_formal_and_prototype_targets_to_separate_cores() {
  const std::string cmake_text = read_text_file(fs::path{DASALL_APPS_TUI_CMAKE});

  assert_true(cmake_text.find("add_library(dasall_tui_core STATIC") != std::string::npos,
              "apps/tui CMake should materialize a fake-free dasall_tui_core");
  assert_true(cmake_text.find("add_library(dasall_tui_prototype_core STATIC") !=
                  std::string::npos,
              "apps/tui CMake should materialize a dedicated prototype core target");
  assert_true(cmake_text.find("src/data/FakeTuiDataSource.cpp") != std::string::npos,
              "prototype core should continue to own FakeTuiDataSource.cpp");
  assert_true(cmake_text.find("target_link_libraries(dasall-tui") != std::string::npos,
              "apps/tui CMake should keep an explicit formal target link block");
  assert_true(cmake_text.find("    dasall_tui_core\n    dasall_platform") != std::string::npos,
              "formal target should link the fake-free core together with the platform IPC boundary");
  assert_true(cmake_text.find("target_link_libraries(dasall_tui_prototype_core") !=
                  std::string::npos,
              "prototype core should declare its dependency edge explicitly");
}

}  // namespace

int main() {
  try {
    formal_binary_excludes_fake_tui_symbols();
    prototype_binary_still_retains_fake_tui_symbols();
    cmake_wires_formal_and_prototype_targets_to_separate_cores();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
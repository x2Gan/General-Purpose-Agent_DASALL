#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <unistd.h>

#include "support/TestAssertions.h"

#ifndef DASALL_TUI_BINARY_PATH
#define DASALL_TUI_BINARY_PATH "/home/gangan/DASALL/build/vscode-linux-ninja/apps/tui/dasall"
#endif

#ifndef DASALL_TUI_LOGICAL_BINARY_PATH
#define DASALL_TUI_LOGICAL_BINARY_PATH "/home/gangan/DASALL/build/vscode-linux-ninja/apps/tui/dasall-tui"
#endif

#ifndef DASALL_APPS_TUI_CMAKE
#define DASALL_APPS_TUI_CMAKE "/home/gangan/DASALL/apps/tui/CMakeLists.txt"
#endif

#ifndef DASALL_APPS_TUI_MAIN
#define DASALL_APPS_TUI_MAIN "/home/gangan/DASALL/apps/tui/src/main.cpp"
#endif

namespace {

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void test_formal_target_outputs_bare_dasall_binary() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const fs::path tui_binary{DASALL_TUI_BINARY_PATH};

  assert_equal(std::string("dasall"), tui_binary.filename().string(),
               "dasall-tui target should expose bare dasall as its build-tree filename");
  assert_true(fs::is_regular_file(tui_binary),
              "dasall-tui target file should exist as a regular file");
  assert_true(::access(tui_binary.c_str(), X_OK) == 0,
              "dasall-tui target file should be executable");
}

void test_logical_target_artifact_name_is_not_produced() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path logical_binary{DASALL_TUI_LOGICAL_BINARY_PATH};

  assert_true(!fs::exists(logical_binary),
              "formal TUI target should not leave a dasall-tui runtime artifact beside bare dasall");
}

void test_cmake_declares_formal_target_output_and_install_rule() {
  using dasall::tests::support::assert_true;

  const std::string cmake_text =
      read_text_file(std::filesystem::path{DASALL_APPS_TUI_CMAKE});

  assert_true(cmake_text.find("add_executable(dasall-tui") != std::string::npos,
              "apps/tui CMake should declare the formal dasall-tui target");
  assert_true(cmake_text.find("src/data/DaemonTuiDataSource.cpp") != std::string::npos,
              "formal target should compile the daemon-backed data source");
  assert_true(cmake_text.find("src/ipc/TuiIpcController.cpp") != std::string::npos,
              "formal target should compile the TUI IPC controller");
  assert_true(cmake_text.find("DASALL_TUI_FORMAL_ENTRYPOINT=1") != std::string::npos,
              "formal target should compile main.cpp in formal entrypoint mode");
  assert_true(cmake_text.find("OUTPUT_NAME dasall") != std::string::npos,
              "formal target should publish bare dasall as its runtime artifact name");
  assert_true(cmake_text.find("install(TARGETS dasall-tui") != std::string::npos,
              "formal target should provide an install rule");
  assert_true(cmake_text.find("RUNTIME DESTINATION ${DASALL_INSTALL_BINDIR}") != std::string::npos,
              "formal target should install the runtime artifact through the repository bindir variable");
  assert_true(cmake_text.find("dasall_platform") != std::string::npos,
              "formal target should link the platform IPC boundary needed by the daemon data source");
  assert_true(cmake_text.find("install(TARGETS dasall_tui_prototype") == std::string::npos,
              "prototype target should remain non-installed after formal target introduction");
}

void test_main_separates_formal_and_prototype_entrypoints() {
  using dasall::tests::support::assert_true;

  const std::string main_text =
      read_text_file(std::filesystem::path{DASALL_APPS_TUI_MAIN});

  assert_true(main_text.find("#if DASALL_TUI_FORMAL_ENTRYPOINT") != std::string::npos,
              "main.cpp should keep a compile-time formal entrypoint switch");
  assert_true(main_text.find("DaemonTuiDataSource") != std::string::npos,
              "formal entrypoint should default to the daemon-backed data source");
  assert_true(main_text.find("planning_tools") != std::string::npos,
              "prototype entrypoint should keep the deterministic fake scenario");
  assert_true(main_text.find("selector_preview_mode") != std::string::npos,
              "prototype entrypoint should keep the fake selector preview path");
}

}  // namespace

int main() {
  try {
    test_formal_target_outputs_bare_dasall_binary();
    test_logical_target_artifact_name_is_not_produced();
    test_cmake_declares_formal_target_output_and_install_rule();
    test_main_separates_formal_and_prototype_entrypoints();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
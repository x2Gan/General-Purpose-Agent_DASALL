#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "support/TestAssertions.h"

#ifndef DASALL_TUI_UNIT_TEST_DIR
#define DASALL_TUI_UNIT_TEST_DIR "/home/gangan/DASALL/tests/unit/tui"
#endif

#ifndef DASALL_TUI_UNIT_TOP_LEVEL_CMAKE
#define DASALL_TUI_UNIT_TOP_LEVEL_CMAKE "/home/gangan/DASALL/tests/unit/CMakeLists.txt"
#endif

#ifndef DASALL_TUI_UNIT_LOCAL_CMAKE
#define DASALL_TUI_UNIT_LOCAL_CMAKE "/home/gangan/DASALL/tests/unit/tui/CMakeLists.txt"
#endif

#ifndef DASALL_TUI_GOLDEN_FIXTURE_DIR
#define DASALL_TUI_GOLDEN_FIXTURE_DIR "/home/gangan/DASALL/tests/fixtures/tui/golden"
#endif

namespace {

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void test_tui_unit_directory_is_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path unit_dir{DASALL_TUI_UNIT_TEST_DIR};

  assert_true(fs::is_directory(unit_dir),
              "tui unit topology should materialize its own unit test directory");
  assert_true(fs::is_regular_file(unit_dir / "CMakeLists.txt"),
              "tui unit topology should expose a dedicated unit CMake entrypoint");
  assert_true(fs::is_regular_file(unit_dir / "TuiUnitTopologySmokeTest.cpp"),
              "tui unit topology should keep the tracked topology smoke source");
  assert_true(!fs::exists(unit_dir / "placeholder.cpp"),
              "tui unit topology should not regress to a placeholder-only unit tree");
}

void test_tui_unit_top_level_cmake_registers_tui_subdirectory() {
  using dasall::tests::support::assert_true;

  const std::string top_level_cmake =
      read_text_file(std::filesystem::path{DASALL_TUI_UNIT_TOP_LEVEL_CMAKE});

  assert_true(top_level_cmake.find("add_subdirectory(tui)") != std::string::npos,
              "tests/unit/CMakeLists.txt should register the tui unit subtree");
  assert_true(top_level_cmake.find("${DASALL_TUI_UNIT_TEST_EXECUTABLE_TARGETS}") !=
                  std::string::npos,
              "tests/unit/CMakeLists.txt should aggregate tui unit executables at top-level");
}

void test_tui_unit_local_cmake_registers_future_focused_tests() {
  using dasall::tests::support::assert_true;

  const std::string local_cmake =
      read_text_file(std::filesystem::path{DASALL_TUI_UNIT_LOCAL_CMAKE});

  assert_true(local_cmake.find("TuiScreenModelTest") != std::string::npos,
              "tui unit CMake should register TuiScreenModelTest for discoverability");
  assert_true(local_cmake.find("TuiReducerTransitionTest") != std::string::npos,
              "tui unit CMake should register TuiReducerTransitionTest for discoverability");
  assert_true(local_cmake.find("TuiComposerTest") != std::string::npos,
              "tui unit CMake should register TuiComposerTest for discoverability");
  assert_true(local_cmake.find("TuiDesignTokensTest") != std::string::npos,
              "tui unit CMake should register TuiDesignTokensTest for discoverability");
  assert_true(local_cmake.find("TuiMainLayoutSnapshotTest") != std::string::npos,
              "tui unit CMake should register TuiMainLayoutSnapshotTest for discoverability");
  assert_true(local_cmake.find("unit;tui") != std::string::npos,
              "tui unit CMake should assign unit;tui labels");
  assert_true(local_cmake.find("unit;snapshot;tui") != std::string::npos,
              "tui unit CMake should assign snapshot labels separately from plain unit tests");
}

void test_tui_snapshot_fixture_directory_is_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path golden_dir{DASALL_TUI_GOLDEN_FIXTURE_DIR};

  assert_true(fs::is_directory(golden_dir),
              "tui snapshot topology should materialize the golden fixture directory");
  assert_true(fs::is_regular_file(golden_dir / "README.md"),
              "tui snapshot topology should keep a tracked golden fixture placeholder");
}

}  // namespace

int main() {
  try {
    test_tui_unit_directory_is_materialized();
    test_tui_unit_top_level_cmake_registers_tui_subdirectory();
    test_tui_unit_local_cmake_registers_future_focused_tests();
    test_tui_snapshot_fixture_directory_is_materialized();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
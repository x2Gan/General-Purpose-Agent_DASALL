#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "support/TestAssertions.h"

#ifndef DASALL_TUI_INTEGRATION_TEST_DIR
#define DASALL_TUI_INTEGRATION_TEST_DIR "/home/gangan/DASALL/tests/integration/tui"
#endif

#ifndef DASALL_TUI_INTEGRATION_TOP_LEVEL_CMAKE
#define DASALL_TUI_INTEGRATION_TOP_LEVEL_CMAKE "/home/gangan/DASALL/tests/integration/CMakeLists.txt"
#endif

#ifndef DASALL_TUI_INTEGRATION_LOCAL_CMAKE
#define DASALL_TUI_INTEGRATION_LOCAL_CMAKE "/home/gangan/DASALL/tests/integration/tui/CMakeLists.txt"
#endif

#ifndef DASALL_TUI_GOLDEN_FIXTURE_DIR
#define DASALL_TUI_GOLDEN_FIXTURE_DIR "/home/gangan/DASALL/tests/fixtures/tui/golden"
#endif

#ifndef DASALL_APPS_TOP_LEVEL_CMAKE
#define DASALL_APPS_TOP_LEVEL_CMAKE "/home/gangan/DASALL/apps/CMakeLists.txt"
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

void test_tui_integration_directory_is_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path integration_dir{DASALL_TUI_INTEGRATION_TEST_DIR};

  assert_true(fs::is_directory(integration_dir),
              "tui integration topology should materialize its own integration test directory");
  assert_true(fs::is_regular_file(integration_dir / "CMakeLists.txt"),
              "tui integration topology should expose a dedicated integration CMake entrypoint");
  assert_true(fs::is_regular_file(integration_dir / "TuiIntegrationTopologySmokeTest.cpp"),
              "tui integration topology should keep the tracked integration topology source");
  assert_true(!fs::exists(integration_dir / "placeholder.cpp"),
              "tui integration topology should not regress to a placeholder-only integration tree");
}

void test_tui_integration_top_level_cmake_registers_tui_subdirectory() {
  using dasall::tests::support::assert_true;

  const std::string top_level_cmake =
      read_text_file(std::filesystem::path{DASALL_TUI_INTEGRATION_TOP_LEVEL_CMAKE});

  assert_true(top_level_cmake.find("add_subdirectory(tui)") != std::string::npos,
              "tests/integration/CMakeLists.txt should register the tui integration subtree");
  assert_true(top_level_cmake.find("${DASALL_APPS_TUI_INTEGRATION_TEST_EXECUTABLE_TARGETS}") !=
                  std::string::npos,
              "tests/integration/CMakeLists.txt should aggregate tui integration executables at top-level");
}

void test_tui_integration_local_cmake_registers_future_app_tests() {
  using dasall::tests::support::assert_true;

  const std::string local_cmake =
      read_text_file(std::filesystem::path{DASALL_TUI_INTEGRATION_LOCAL_CMAKE});

  assert_true(local_cmake.find("TuiTestTopologyDiscoverability") != std::string::npos,
              "tui integration CMake should register TuiTestTopologyDiscoverability");
  assert_true(local_cmake.find("TuiAppStartupTest") != std::string::npos,
              "tui integration CMake should register TuiAppStartupTest for discoverability");
  assert_true(local_cmake.find("TuiAppStartupFailureTest") != std::string::npos,
              "tui integration CMake should register TuiAppStartupFailureTest for discoverability");
  assert_true(local_cmake.find("TuiPrototypeSmokeTest") != std::string::npos,
              "tui integration CMake should register TuiPrototypeSmokeTest for discoverability");
  assert_true(local_cmake.find("TuiPrototypeBuildSmokeTest") != std::string::npos,
              "tui integration CMake should register TuiPrototypeBuildSmokeTest for focused build validation");
  assert_true(local_cmake.find("integration;tui") != std::string::npos,
              "tui integration CMake should assign integration;tui labels");
}

void test_tui_prototype_target_is_wired_into_apps_graph() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const std::string apps_top_level_cmake =
      read_text_file(std::filesystem::path{DASALL_APPS_TOP_LEVEL_CMAKE});
  const std::string apps_tui_cmake =
      read_text_file(std::filesystem::path{DASALL_APPS_TUI_CMAKE});

  assert_true(apps_top_level_cmake.find("add_subdirectory(tui)") != std::string::npos,
              "apps/CMakeLists.txt should register the tui app subtree");
  assert_true(fs::is_regular_file(std::filesystem::path{DASALL_APPS_TUI_MAIN}),
              "apps/tui should materialize a prototype main.cpp entrypoint");
  assert_true(apps_tui_cmake.find("add_executable(dasall_tui_prototype") != std::string::npos,
              "apps/tui/CMakeLists.txt should declare the dasall_tui_prototype target");
  assert_true(apps_tui_cmake.find("install(TARGETS dasall_tui_prototype") == std::string::npos,
              "prototype target should stay non-installed during the fake-only phase");
  assert_true(apps_tui_cmake.find("dasall_access") == std::string::npos,
              "prototype target should not link access implementation during the fake-only phase");
  assert_true(apps_tui_cmake.find("dasall_runtime") == std::string::npos,
              "prototype target should not link runtime implementation during the fake-only phase");
  assert_true(apps_tui_cmake.find("dasall_apps_runtime_support") == std::string::npos,
              "prototype target should not link daemon runtime support during the fake-only phase");
}

void test_tui_snapshot_fixture_directory_is_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path golden_dir{DASALL_TUI_GOLDEN_FIXTURE_DIR};

  assert_true(fs::is_directory(golden_dir),
              "tui integration topology should materialize the golden fixture directory");
  assert_true(fs::is_regular_file(golden_dir / "README.md"),
              "tui integration topology should keep a tracked golden placeholder");
}

}  // namespace

int main() {
  try {
    test_tui_integration_directory_is_materialized();
    test_tui_integration_top_level_cmake_registers_tui_subdirectory();
    test_tui_integration_local_cmake_registers_future_app_tests();
    test_tui_prototype_target_is_wired_into_apps_graph();
    test_tui_snapshot_fixture_directory_is_materialized();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
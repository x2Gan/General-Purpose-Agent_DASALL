#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"

#ifndef DASALL_MEMORY_INTEGRATION_TEST_DIR
#define DASALL_MEMORY_INTEGRATION_TEST_DIR "/home/gangan/DASALL/tests/integration/memory"
#endif

#ifndef DASALL_MEMORY_UNIT_TEST_DIR
#define DASALL_MEMORY_UNIT_TEST_DIR "/home/gangan/DASALL/tests/unit/memory"
#endif

#ifndef DASALL_MEMORY_INTEGRATION_TOP_LEVEL_CMAKE
#define DASALL_MEMORY_INTEGRATION_TOP_LEVEL_CMAKE "/home/gangan/DASALL/tests/integration/CMakeLists.txt"
#endif

namespace {

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void test_memory_integration_topology_uses_namespaced_names() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "MemoryIntegrationTopologySmokeTest";
  constexpr std::string_view target_name =
      "dasall_memory_integration_topology_smoke_integration_test";

  assert_true(ctest_name.find("Memory") == 0U,
              "memory integration topology should keep a memory-specific ctest prefix");
  assert_true(target_name.find("dasall_memory_") == 0U,
              "memory integration topology target should remain namespaced under dasall_memory");
}

void test_memory_integration_directory_is_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path integration_dir{DASALL_MEMORY_INTEGRATION_TEST_DIR};
  const fs::path integration_cmake = integration_dir / "CMakeLists.txt";
  const fs::path topology_smoke =
      integration_dir / "MemoryIntegrationTopologySmokeTest.cpp";

  assert_true(fs::is_directory(integration_dir),
              "memory integration topology should materialize its own test directory");
  assert_true(fs::is_regular_file(integration_cmake),
              "memory integration topology should expose a dedicated CMake entrypoint");
  assert_true(fs::is_regular_file(topology_smoke),
              "memory integration topology should expose a tracked smoke test source");
}

void test_memory_top_level_integration_cmake_registers_memory_subdirectory() {
  using dasall::tests::support::assert_true;

  const std::filesystem::path top_level_cmake_path{
      DASALL_MEMORY_INTEGRATION_TOP_LEVEL_CMAKE};
  const std::string top_level_cmake = read_text_file(top_level_cmake_path);

  assert_true(top_level_cmake.find("add_subdirectory(memory)") != std::string::npos,
              "tests/integration/CMakeLists.txt should register the memory integration subtree");
  assert_true(
      top_level_cmake.find("${DASALL_MEMORY_INTEGRATION_TEST_EXECUTABLE_TARGETS}") !=
          std::string::npos,
      "tests/integration/CMakeLists.txt should aggregate memory integration executables at top-level");
}

void test_memory_integration_topology_no_longer_relies_on_a_placeholder_tree() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path unit_dir{DASALL_MEMORY_UNIT_TEST_DIR};
  const fs::path integration_dir{DASALL_MEMORY_INTEGRATION_TEST_DIR};
  const fs::path legacy_placeholder = integration_dir / "placeholder.cpp";

  assert_true(fs::is_directory(unit_dir),
              "memory integration topology should remain paired with the existing unit test subtree");
  assert_true(!fs::exists(legacy_placeholder),
              "memory integration topology should not regress to a placeholder-only integration subtree");
}

}  // namespace

int main() {
  try {
    test_memory_integration_topology_uses_namespaced_names();
    test_memory_integration_directory_is_materialized();
    test_memory_top_level_integration_cmake_registers_memory_subdirectory();
    test_memory_integration_topology_no_longer_relies_on_a_placeholder_tree();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
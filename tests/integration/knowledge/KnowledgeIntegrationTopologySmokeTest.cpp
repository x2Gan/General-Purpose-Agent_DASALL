#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"

#ifndef DASALL_KNOWLEDGE_MODULE_CMAKE
#define DASALL_KNOWLEDGE_MODULE_CMAKE "/home/gangan/DASALL/knowledge/CMakeLists.txt"
#endif

#ifndef DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR
#define DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR "/home/gangan/DASALL/knowledge/include"
#endif

#ifndef DASALL_KNOWLEDGE_UNIT_TEST_DIR
#define DASALL_KNOWLEDGE_UNIT_TEST_DIR "/home/gangan/DASALL/tests/unit/knowledge"
#endif

#ifndef DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR
#define DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR "/home/gangan/DASALL/tests/integration/knowledge"
#endif

#ifndef DASALL_KNOWLEDGE_INTEGRATION_TOP_LEVEL_CMAKE
#define DASALL_KNOWLEDGE_INTEGRATION_TOP_LEVEL_CMAKE "/home/gangan/DASALL/tests/integration/CMakeLists.txt"
#endif

namespace {

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void test_knowledge_integration_topology_uses_namespaced_names() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "KnowledgeIntegrationTopologySmokeTest";
  constexpr std::string_view target_name =
      "dasall_knowledge_integration_topology_smoke_integration_test";

  assert_true(ctest_name.find("Knowledge") == 0U,
              "knowledge integration topology should keep a knowledge-specific ctest prefix");
  assert_true(target_name.find("dasall_knowledge_") == 0U,
              "knowledge integration topology target should remain namespaced under dasall_knowledge");
}

void test_knowledge_public_include_root_is_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path include_dir{DASALL_KNOWLEDGE_PUBLIC_INCLUDE_DIR};

  assert_true(fs::is_directory(include_dir),
              "knowledge build skeleton should materialize the public include root");
  assert_true(fs::is_regular_file(include_dir / "KnowledgeTypes.h"),
              "knowledge build skeleton should track KnowledgeTypes.h");
  assert_true(fs::is_regular_file(include_dir / "KnowledgeErrors.h"),
              "knowledge build skeleton should track KnowledgeErrors.h");
  assert_true(fs::is_regular_file(include_dir / "IKnowledgeService.h"),
              "knowledge build skeleton should track IKnowledgeService.h");
}

void test_knowledge_module_cmake_no_longer_relies_on_placeholder_only_layout() {
  using dasall::tests::support::assert_true;

  const std::string module_cmake =
      read_text_file(std::filesystem::path{DASALL_KNOWLEDGE_MODULE_CMAKE});

  assert_true(module_cmake.find("FILE_SET public_headers") != std::string::npos,
              "knowledge module CMake should export a public header file set");
  assert_true(module_cmake.find("KnowledgeBuildSkeleton.cpp") != std::string::npos,
              "knowledge module CMake should compile the dedicated build skeleton source");
  assert_true(module_cmake.find("placeholder.cpp") == std::string::npos,
              "knowledge module CMake should not regress to a placeholder-only source layout");
}

void test_knowledge_top_level_integration_cmake_registers_knowledge_subdirectory() {
  using dasall::tests::support::assert_true;

  const std::string top_level_cmake = read_text_file(
      std::filesystem::path{DASALL_KNOWLEDGE_INTEGRATION_TOP_LEVEL_CMAKE});

  assert_true(top_level_cmake.find("add_subdirectory(knowledge)") != std::string::npos,
              "tests/integration/CMakeLists.txt should register the knowledge integration subtree");
  assert_true(top_level_cmake.find("${DASALL_KNOWLEDGE_INTEGRATION_TEST_EXECUTABLE_TARGETS}") !=
                  std::string::npos,
              "tests/integration/CMakeLists.txt should aggregate knowledge integration executables at top-level");
}

void test_knowledge_test_subtrees_are_materialized() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path unit_dir{DASALL_KNOWLEDGE_UNIT_TEST_DIR};
  const fs::path integration_dir{DASALL_KNOWLEDGE_INTEGRATION_TEST_DIR};

  assert_true(fs::is_regular_file(unit_dir / "CMakeLists.txt"),
              "knowledge unit skeleton should expose a dedicated CMake entrypoint");
  assert_true(fs::is_regular_file(unit_dir / "KnowledgeInterfaceSurfaceSkeletonTest.cpp"),
              "knowledge unit skeleton should expose a tracked surface test source");
  assert_true(fs::is_regular_file(integration_dir / "CMakeLists.txt"),
              "knowledge integration skeleton should expose a dedicated CMake entrypoint");
  assert_true(fs::is_regular_file(integration_dir / "KnowledgeIntegrationTopologySmokeTest.cpp"),
              "knowledge integration skeleton should expose a tracked smoke test source");
  assert_true(!fs::exists(integration_dir / "placeholder.cpp"),
              "knowledge integration skeleton should not regress to a placeholder-only subtree");
}

}  // namespace

int main() {
  try {
    test_knowledge_integration_topology_uses_namespaced_names();
    test_knowledge_public_include_root_is_materialized();
    test_knowledge_module_cmake_no_longer_relies_on_placeholder_only_layout();
    test_knowledge_top_level_integration_cmake_registers_knowledge_subdirectory();
    test_knowledge_test_subtrees_are_materialized();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
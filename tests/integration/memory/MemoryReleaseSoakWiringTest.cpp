#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;
namespace fs = std::filesystem;

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path);
  assert_true(stream.is_open(),
              "memory release soak wiring test should open " + path.string());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void assert_contains_all(std::string_view text,
                         std::initializer_list<std::string_view> needles,
                         std::string_view message_prefix) {
  for (const auto needle : needles) {
    assert_true(text.find(needle) != std::string_view::npos,
                std::string(message_prefix) + " should contain '" +
                    std::string(needle) + "'");
  }
}

void test_memory_integration_cmake_registers_release_soak_probe_target() {
  const auto cmake_text = read_text_file(
      repository_root() / "tests" / "integration" / "memory" / "CMakeLists.txt");

  assert_contains_all(
      cmake_text,
      {
          "dasall_memory_release_soak_probe_integration_test",
          "MemoryReleaseSoakProbeTest",
          "MemoryReleaseSoakProbeTest.cpp",
      },
      "memory integration cmake release soak probe registration");
}

void test_infra_release_soak_script_collects_memory_summary() {
  const auto script_text = read_text_file(
      repository_root() / "scripts" / "packaging" /
      "infra_release_soak_gate.sh");

  assert_contains_all(
      script_text,
      {
          "run_memory_release_soak_probe()",
          "dasall_memory_release_soak_probe_integration_test",
          "memory-release-soak.log",
          "memory-release-soak-summary.json",
          "memory_release_soak_summary",
          "memory_store_latency",
          "memory_summary_fallback_rate",
      },
      "infra release soak gate script memory wiring");
}

void test_release_workflow_builds_memory_release_soak_probe() {
  const auto workflow_text = read_text_file(
      repository_root() / ".github" / "workflows" /
      "release-package-gate.yml");

  assert_contains_all(
      workflow_text,
      {
          "dasall_memory_release_soak_probe_integration_test",
          "infra_release_soak_gate.sh",
      },
      "release workflow memory release soak wiring");
}

void test_packaging_readme_describes_memory_release_soak_artifact() {
  const auto readme_text = read_text_file(
      repository_root() / "scripts" / "packaging" / "README.md");

  assert_contains_all(
      readme_text,
      {
          "MemoryReleaseSoakProbeTest",
          "memory-release-soak-summary.json",
          "store latency / wal size / maintenance lag / writeback partial rate / vector recall@k / summary fallback rate",
      },
      "packaging readme memory release soak artifact contract");
}

}  // namespace

int main() {
  try {
    test_memory_integration_cmake_registers_release_soak_probe_target();
    test_infra_release_soak_script_collects_memory_summary();
    test_release_workflow_builds_memory_release_soak_probe();
    test_packaging_readme_describes_memory_release_soak_artifact();
  } catch (const std::exception& ex) {
    std::cerr << "[MemoryReleaseSoakWiringTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
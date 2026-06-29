#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

#ifndef DASALL_REPO_ROOT
#error "MemoryHistoricalArtifactRemovedTest requires DASALL_REPO_ROOT"
#endif

namespace {

struct Violation {
  std::filesystem::path file_path;
  std::size_t line_number = 0U;
  std::string evidence;
};

[[nodiscard]] std::filesystem::path repo_root() {
  return std::filesystem::path(DASALL_REPO_ROOT);
}

[[nodiscard]] std::string strip_line_comment(std::string_view value) {
  const std::size_t comment_pos = value.find("//");
  if (comment_pos == std::string_view::npos) {
    return std::string(value);
  }

  return std::string(value.substr(0U, comment_pos));
}

[[nodiscard]] std::vector<Violation> scan_forbidden_tokens(
    const std::filesystem::path& file_path,
    const std::vector<std::string_view>& forbidden_tokens) {
  std::vector<Violation> violations;
  std::ifstream stream(file_path);
  std::string line;
  std::size_t line_number = 0U;

  while (std::getline(stream, line)) {
    ++line_number;
    const std::string sanitized = strip_line_comment(line);
    for (const auto token : forbidden_tokens) {
      if (sanitized.find(token) != std::string::npos) {
        violations.push_back(Violation{
            .file_path = file_path,
            .line_number = line_number,
            .evidence = std::string(token),
        });
      }
    }
  }

  return violations;
}

[[nodiscard]] std::string summarize_violations(const std::vector<Violation>& violations) {
  std::ostringstream stream;
  for (const auto& violation : violations) {
    stream << violation.file_path.lexically_relative(repo_root()).string() << ':'
           << violation.line_number << " => " << violation.evidence << '\n';
  }

  return stream.str();
}

void test_memory_source_tree_no_longer_contains_removed_build_anchor() {
  namespace fs = std::filesystem;

  using dasall::tests::support::assert_true;

  const fs::path source_root = repo_root() / "memory" / "src";

  assert_true(!fs::exists(source_root / "MemoryBuildSkeleton.cpp"),
              "memory source tree should no longer carry the removed MemoryBuildSkeleton.cpp anchor");
  assert_true(!fs::exists(source_root / "placeholder.cpp"),
              "memory source tree should not regress to placeholder.cpp after historical cleanup");
}

void test_memory_cmake_no_longer_references_removed_artifacts() {
  using dasall::tests::support::assert_true;

  const std::vector<Violation> violations = scan_forbidden_tokens(
      repo_root() / "memory" / "CMakeLists.txt",
      {
          "MemoryBuildSkeleton.cpp",
          "placeholder.cpp",
          "keep_library_non_empty",
      });

  assert_true(violations.empty(),
              "memory/CMakeLists.txt should not retain removed historical build-artifact references:\n" +
                  summarize_violations(violations));
}

}  // namespace

int main() {
  try {
    test_memory_source_tree_no_longer_contains_removed_build_anchor();
    test_memory_cmake_no_longer_references_removed_artifacts();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
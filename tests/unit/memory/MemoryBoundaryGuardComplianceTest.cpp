#include <algorithm>
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
#error "MemoryBoundaryGuardComplianceTest requires DASALL_REPO_ROOT"
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

[[nodiscard]] std::string trim_leading(std::string_view value) {
  const std::size_t start = value.find_first_not_of(" \t");
  if (start == std::string_view::npos) {
    return std::string();
  }

  return std::string(value.substr(start));
}

[[nodiscard]] std::string strip_line_comment(std::string_view value) {
  const std::size_t comment_pos = value.find("//");
  if (comment_pos == std::string_view::npos) {
    return std::string(value);
  }

  return std::string(value.substr(0U, comment_pos));
}

[[nodiscard]] std::vector<std::filesystem::path> collect_source_files(
    const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;

  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const std::filesystem::path path = entry.path();
    const std::string extension = path.extension().string();
    if (extension == ".cpp" || extension == ".h" || extension == ".hpp") {
      files.push_back(path);
    }
  }

  std::sort(files.begin(), files.end());
  return files;
}

[[nodiscard]] std::vector<Violation> scan_include_lines(
    const std::vector<std::filesystem::path>& files,
    const std::vector<std::string_view>& forbidden_tokens) {
  std::vector<Violation> violations;

  for (const auto& file_path : files) {
    std::ifstream stream(file_path);
    std::string line;
    std::size_t line_number = 0U;

    while (std::getline(stream, line)) {
      ++line_number;
      const std::string trimmed = trim_leading(line);
      if (!trimmed.starts_with("#include")) {
        continue;
      }

      for (const auto token : forbidden_tokens) {
        if (trimmed.find(token) != std::string::npos) {
          violations.push_back(Violation{
              .file_path = file_path,
              .line_number = line_number,
              .evidence = std::string(token),
          });
        }
      }
    }
  }

  return violations;
}

[[nodiscard]] std::vector<Violation> scan_forbidden_tokens(
    const std::vector<std::filesystem::path>& files,
    const std::vector<std::string_view>& forbidden_tokens) {
  std::vector<Violation> violations;

  for (const auto& file_path : files) {
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
  }

  return violations;
}

[[nodiscard]] std::vector<Violation> scan_cmake_forbidden_tokens(
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

[[nodiscard]] std::string summarize_violations(const std::vector<Violation>& violations,
                                               std::size_t max_items = 8U) {
  std::ostringstream stream;
  const std::size_t count = std::min(max_items, violations.size());
  for (std::size_t index = 0U; index < count; ++index) {
    const auto& violation = violations[index];
    stream << violation.file_path.lexically_relative(repo_root()).string() << ':'
           << violation.line_number << " => " << violation.evidence << '\n';
  }

  if (violations.size() > count) {
    stream << "... and " << (violations.size() - count) << " more";
  }

  return stream.str();
}

void test_memory_sources_do_not_include_forbidden_boundary_owners() {
  using dasall::tests::support::assert_true;

  const std::filesystem::path memory_root = repo_root() / "memory";
  std::vector<std::filesystem::path> files = collect_source_files(memory_root / "include");
  const std::vector<std::filesystem::path> src_files = collect_source_files(memory_root / "src");
  files.insert(files.end(), src_files.begin(), src_files.end());

  const std::vector<Violation> violations = scan_include_lines(
      files,
      {
          "llm/",
          "runtime/",
          "tools/",
          "apps/",
          "PromptComposer",
          "PromptRegistry",
          "RecoveryManager",
          "AgentOrchestrator",
          "ToolExecutor",
      });

  assert_true(
      violations.empty(),
      "memory sources must not include llm/runtime/tools/apps private implementation headers or boundary owners:\n" +
          summarize_violations(violations));
}

void test_memory_target_does_not_link_or_include_forbidden_subsystems_in_cmake() {
  using dasall::tests::support::assert_true;

  const std::vector<Violation> violations = scan_cmake_forbidden_tokens(
      repo_root() / "memory" / "CMakeLists.txt",
      {
          "dasall_llm",
          "dasall_runtime",
          "dasall_tools",
          "dasall_access",
          "dasall_apps",
          "${CMAKE_SOURCE_DIR}/llm",
          "${CMAKE_SOURCE_DIR}/runtime",
          "${CMAKE_SOURCE_DIR}/tools",
          "${CMAKE_SOURCE_DIR}/apps",
          "${PROJECT_SOURCE_DIR}/llm",
          "${PROJECT_SOURCE_DIR}/runtime",
          "${PROJECT_SOURCE_DIR}/tools",
          "${PROJECT_SOURCE_DIR}/apps",
      });

  assert_true(
      violations.empty(),
      "dasall_memory CMake target must not link or include forbidden subsystem private roots:\n" +
          summarize_violations(violations));
}

void test_memory_public_surface_does_not_expose_prompt_recovery_orchestrator_types() {
  using dasall::tests::support::assert_true;

  const std::vector<std::filesystem::path> files = collect_source_files(repo_root() / "memory" / "include");

  const std::vector<Violation> violations = scan_forbidden_tokens(
      files,
      {
          "PromptComposer",
          "PromptRegistry",
          "PromptComposeRequest",
          "PromptPolicy",
          "RecoveryManager",
          "RecoveryDecision",
          "AgentOrchestrator",
          "ToolExecutor",
          "ToolRegistry",
      });

  assert_true(
      violations.empty(),
      "memory public headers must not expose llm prompt, runtime recovery, or agent-control owner types:\n" +
          summarize_violations(violations));
}

}  // namespace

int main() {
  try {
    test_memory_sources_do_not_include_forbidden_boundary_owners();
    test_memory_target_does_not_link_or_include_forbidden_subsystems_in_cmake();
    test_memory_public_surface_does_not_expose_prompt_recovery_orchestrator_types();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
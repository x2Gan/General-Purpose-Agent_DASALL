#include <algorithm>
#include <array>
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
#error "LLMBoundaryGuardComplianceTest requires DASALL_REPO_ROOT"
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

[[nodiscard]] std::vector<std::filesystem::path> collect_source_files(const std::filesystem::path& root) {
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

void test_llm_sources_do_not_include_forbidden_subsystem_privates() {
  using dasall::tests::support::assert_true;

  const std::filesystem::path llm_root = repo_root() / "llm";
  std::vector<std::filesystem::path> files = collect_source_files(llm_root / "include");
  const std::vector<std::filesystem::path> src_files = collect_source_files(llm_root / "src");
  files.insert(files.end(), src_files.begin(), src_files.end());

  const std::vector<Violation> violations = scan_include_lines(
      files,
      {
          "memory/",
          "runtime/",
          "tools/",
          "apps/",
          "ContextOrchestrator",
          "RecoveryManager",
          "AgentOrchestrator",
      });

  assert_true(violations.empty(),
              "llm sources must not include memory/runtime/tools/apps private implementation headers or boundary owners:\n" +
                  summarize_violations(violations));
}

void test_llm_target_does_not_link_or_include_forbidden_subsystems_in_cmake() {
  using dasall::tests::support::assert_true;

  const std::vector<Violation> violations = scan_cmake_forbidden_tokens(
      repo_root() / "llm" / "CMakeLists.txt",
      {
          "dasall_memory",
          "dasall_runtime",
          "dasall_tools",
          "dasall_apps",
          "${PROJECT_SOURCE_DIR}/memory",
          "${PROJECT_SOURCE_DIR}/runtime",
          "${PROJECT_SOURCE_DIR}/tools",
          "${PROJECT_SOURCE_DIR}/apps",
      });

  assert_true(violations.empty(),
              "dasall_llm CMake target must not link or include forbidden subsystem private roots:\n" +
                  summarize_violations(violations));
}

void test_prompt_pipeline_and_composer_do_not_take_on_memory_or_knowledge_retrieval() {
  using dasall::tests::support::assert_true;

  const std::filesystem::path prompt_root = repo_root() / "llm" / "src" / "prompt";
  const std::vector<std::filesystem::path> files = {
      prompt_root / "PromptComposer.h",
      prompt_root / "PromptComposer.cpp",
      prompt_root / "PromptPipeline.h",
      prompt_root / "PromptPipeline.cpp",
  };

  const std::vector<Violation> violations = scan_forbidden_tokens(
      files,
      {
          "ContextOrchestrator",
          "MemoryStore",
          "KnowledgeStore",
          "VectorStore",
          "retrieve_memory",
          "retrieve_knowledge",
          "retrieve_context",
          "query_knowledge",
          "memory_retrieval",
          "knowledge_retrieval",
          "knowledge/",
          "memory/",
      });

  assert_true(violations.empty(),
              "PromptPipeline and PromptComposer must stay within llm-local selection/composition governance and avoid memory or knowledge retrieval responsibilities:\n" +
                  summarize_violations(violations));
}

}  // namespace

int main() {
  try {
    test_llm_sources_do_not_include_forbidden_subsystem_privates();
    test_llm_target_does_not_link_or_include_forbidden_subsystems_in_cmake();
    test_prompt_pipeline_and_composer_do_not_take_on_memory_or_knowledge_retrieval();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
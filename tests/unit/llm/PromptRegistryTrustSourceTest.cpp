#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "prompt/PromptQuery.h"
#include "prompt/PromptRegistryConfig.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/PromptRegistry.h"

namespace {

class TempDirectory {
 public:
  explicit TempDirectory(const std::string& prefix) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(unique));
    std::filesystem::create_directories(path_);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

void create_prompt_package(const std::filesystem::path& root,
                           const std::string& version,
                           const std::string& trusted_source) {
  const std::filesystem::path package_root = root / "planner" / version;

  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "package_id: planner." + version + "\n"
             "prompt_id: planner\n"
             "version: \"" + version + "\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: " + trusted_source + "\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "model_family: openai_compatible\n"
             "task_types:\n"
             "  - plan\n"
             "tags:\n"
             "  - planner\n");
  write_file(package_root / "system.md", "system-" + version);
  write_file(package_root / "task.md", "task-" + version + " {{user_goal}}");
}

dasall::llm::prompt::PromptRegistry make_registry(const std::filesystem::path& root,
                                                  std::vector<std::string> trusted_sources) {
  using dasall::llm::prompt::PromptRegistry;
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::tests::support::assert_true;

  PromptRegistry registry;
  const PromptRegistryConfig config{
    .asset_root = root.generic_string(),
    .trusted_sources = std::move(trusted_sources),
  };

  assert_true(registry.init(config),
              "PromptRegistry should initialize against the temporary prompt catalog");
  return registry;
}

dasall::llm::prompt::PromptQuery make_query(std::vector<std::string> trusted_sources = {}) {
  return dasall::llm::prompt::PromptQuery{
      .stage = "planning",
      .task_type = "plan",
      .language = "zh-cn",
      .model_family = "openai_compatible",
      .prompt_release_id = std::string(),
      .scene_id = std::string(),
      .persona_id = std::string(),
      .profile_id = std::string(),
      .available_tools = {},
      .trusted_sources = std::move(trusted_sources),
  };
}

void test_registry_allows_trusted_source_hit() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_registry_trusted_source_hit");
  create_prompt_package(temp_directory.path(), "2026.04.11", "profiles");

  auto registry = make_registry(temp_directory.path(), {"profiles", "snapshot"});
  const auto result = registry.select(make_query({"profiles"}));

  assert_true(result.has_consistent_values(),
              "PromptRegistry should return a consistent result for allowed trusted sources");
  assert_true(result.release.has_value(),
              "PromptRegistry should select a release when trusted source is allowed");
  assert_equal(std::string("profiles"), result.trusted_sources_matched.front(),
               "PromptRegistry should expose the matched trusted source in selection metadata");
}

void test_registry_rejects_query_source_that_widens_registry_allowlist() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_registry_trusted_source_intersection");
  create_prompt_package(temp_directory.path(), "2026.04.11", "snapshot");

  auto registry = make_registry(temp_directory.path(), {"profiles"});
  const auto result = registry.select(make_query({"snapshot"}));

  assert_true(result.has_consistent_values(),
              "PromptRegistry should surface trusted source widening as a consistent failure");
  assert_true(!result.release.has_value(),
              "PromptRegistry should fail closed when query trusted_sources exceed registry policy");
  assert_true(result.code == ResultCode::PolicyDenied,
              "PromptRegistry should classify trusted source widening as policy denial");
  assert_true(result.selection_reason == "trusted_source_rejected",
              "PromptRegistry should record the trusted source rejection reason");
}

void test_registry_rejects_when_no_trusted_source_allowlist_exists() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_registry_trusted_source_missing");
  create_prompt_package(temp_directory.path(), "2026.04.11", "profiles");

  auto registry = make_registry(temp_directory.path(), {});
  const auto result = registry.select(make_query());

  assert_true(result.has_consistent_values(),
              "PromptRegistry should return a consistent failure when trusted source policy is missing");
  assert_true(!result.release.has_value(),
              "PromptRegistry should fail closed without any trusted source allowlist");
  assert_true(result.code == ResultCode::PolicyDenied,
              "PromptRegistry should classify missing trusted source policy as policy denial");
  assert_true(result.selection_reason == "trusted_source_allowlist_missing",
              "PromptRegistry should record the fail-closed trusted source reason");
}

}  // namespace

int main() {
  try {
    test_registry_allows_trusted_source_hit();
    test_registry_rejects_query_source_that_widens_registry_allowlist();
    test_registry_rejects_when_no_trusted_source_allowlist_exists();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
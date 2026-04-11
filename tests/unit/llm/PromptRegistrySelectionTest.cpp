#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

struct PromptPackageOptions {
  std::string prompt_id = "planner";
  std::string version;
  std::string trusted_source = "profiles";
  std::string scene_id;
  std::string persona_id;
  std::vector<std::string> profile_tags;
  bool default_release = false;
};

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

void append_optional_list(std::string& manifest,
                          const std::string& key,
                          const std::vector<std::string>& values) {
  if (values.empty()) {
    return;
  }

  manifest.append(key);
  manifest.append(":\n");
  for (const auto& value : values) {
    manifest.append("  - ");
    manifest.append(value);
    manifest.push_back('\n');
  }
}

void create_prompt_package(const std::filesystem::path& root,
                           const PromptPackageOptions& options) {
  const std::filesystem::path package_root = root / options.prompt_id / options.version;

  std::string manifest;
  manifest.append("schema_version: \"1\"\n");
  manifest.append("min_loader_version: \"1\"\n");
  manifest.append("package_id: ");
  manifest.append(options.prompt_id);
  manifest.push_back('.');
  manifest.append(options.version);
  manifest.push_back('\n');
  manifest.append("prompt_id: ");
  manifest.append(options.prompt_id);
  manifest.push_back('\n');
  manifest.append("version: \"");
  manifest.append(options.version);
  manifest.append("\"\n");
  manifest.append("stage: planning\n");
  manifest.append("eval_status: stable\n");
  manifest.append("release_scope: stable\n");
  manifest.append("output_schema_ref: schema://planner/default\n");
  manifest.append("trusted_source: ");
  manifest.append(options.trusted_source);
  manifest.push_back('\n');
  manifest.append("default_release: ");
  manifest.append(options.default_release ? "true\n" : "false\n");
  manifest.append("language: zh-cn\n");
  manifest.append("model_family: openai_compatible\n");
  manifest.append("task_types:\n");
  manifest.append("  - plan\n");
  manifest.append("tags:\n");
  manifest.append("  - planner\n");
  if (!options.scene_id.empty()) {
    manifest.append("scene_id: ");
    manifest.append(options.scene_id);
    manifest.push_back('\n');
  }
  if (!options.persona_id.empty()) {
    manifest.append("persona_id: ");
    manifest.append(options.persona_id);
    manifest.push_back('\n');
  }
  append_optional_list(manifest, "profile_tags", options.profile_tags);

  write_file(package_root / "manifest.yaml", manifest);
  write_file(package_root / "system.md", "system-" + options.version);
  write_file(package_root / "task.md", "task-" + options.version + " {{user_goal}}");
}

dasall::llm::prompt::PromptRegistry make_registry(const std::filesystem::path& root) {
  using dasall::llm::prompt::PromptRegistry;
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::tests::support::assert_true;

  PromptRegistry registry;
  const PromptRegistryConfig config{
    .asset_root = root.generic_string(),
    .trusted_sources = {"profiles"},
  };

  assert_true(registry.init(config),
              "PromptRegistry should initialize against the temporary prompt catalog");
  return registry;
}

dasall::llm::prompt::PromptQuery make_query() {
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
      .trusted_sources = {"profiles"},
  };
}

void test_registry_prefers_explicit_release_over_scene_persona_and_default() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_registry_explicit_release");
  create_prompt_package(temp_directory.path(), PromptPackageOptions{
                                                   .version = "2026.04.11",
                                                   .scene_id = "general",
                                                   .persona_id = "planner",
                                                   .profile_tags = {"desktop_full"},
                                                   .default_release = true,
                                               });
  create_prompt_package(temp_directory.path(), PromptPackageOptions{
                                                   .version = "2026.04.12",
                                                   .scene_id = "operator",
                                                   .persona_id = "operator",
                                                   .profile_tags = {"edge_minimal"},
                                                   .default_release = false,
                                               });

  auto registry = make_registry(temp_directory.path());
  auto query = make_query();
  query.prompt_release_id = "planner@2026.04.12";
  query.scene_id = "general";
  query.persona_id = "planner";
  query.profile_id = "desktop_full";

  const auto result = registry.select(query);

  assert_true(result.has_consistent_values(),
              "PromptRegistry should produce a consistent result for an explicit release hit");
  assert_true(result.release.has_value(),
              "PromptRegistry should return the selected PromptRelease on explicit release hit");
  assert_equal(std::string("2026.04.12"), result.selected_version,
               "explicit prompt_release_id should override scene/persona/default selection");
  assert_equal(std::string("explicit_prompt_release_id"), result.selection_reason,
               "selection_reason should record the explicit release path");
}

void test_registry_selects_scene_persona_then_profile_then_default() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_registry_selector_fallbacks");
  create_prompt_package(temp_directory.path(), PromptPackageOptions{
                                                   .version = "2026.04.11",
                                                   .scene_id = "general",
                                                   .persona_id = "planner",
                                                   .profile_tags = {"desktop_full"},
                                                   .default_release = true,
                                               });
  create_prompt_package(temp_directory.path(), PromptPackageOptions{
                                                   .version = "2026.04.12",
                                                   .scene_id = "operator",
                                                   .persona_id = "operator",
                                                   .profile_tags = {"factory_test"},
                                                   .default_release = false,
                                               });
  create_prompt_package(temp_directory.path(), PromptPackageOptions{
                                                   .version = "2026.04.13",
                                                   .profile_tags = {"cloud_full"},
                                                   .default_release = false,
                                               });

  auto registry = make_registry(temp_directory.path());

  auto scene_query = make_query();
  scene_query.scene_id = "operator";
  scene_query.persona_id = "operator";
  scene_query.profile_id = "factory_test";
  const auto scene_result = registry.select(scene_query);
  assert_true(scene_result.release.has_value(),
              "PromptRegistry should select a release for scene/persona-specific queries");
  assert_equal(std::string("2026.04.12"), scene_result.selected_version,
               "scene/persona selectors should outrank profile/default fallbacks");
  assert_equal(std::string("scene_persona_selector"), scene_result.selection_reason,
               "selection_reason should record the scene/persona selector path");

  auto profile_query = make_query();
  profile_query.profile_id = "cloud_full";
  const auto profile_result = registry.select(profile_query);
  assert_true(profile_result.release.has_value(),
              "PromptRegistry should select a release for profile default queries");
  assert_equal(std::string("2026.04.13"), profile_result.selected_version,
               "profile selector should win when no scene/persona selector matches");
  assert_equal(std::string("profile_selector"), profile_result.selection_reason,
               "selection_reason should record the profile selector path");

  auto default_query = make_query();
  const auto first_default = registry.select(default_query);
  const auto second_default = registry.select(default_query);
  assert_true(first_default.release.has_value(),
              "PromptRegistry should fall back to the package default release");
  assert_equal(std::string("2026.04.11"), first_default.selected_version,
               "default selection should return the package default release");
  assert_equal(std::string("default_release"), first_default.selection_reason,
               "selection_reason should record the default release path");
  assert_equal(first_default.selected_version, second_default.selected_version,
               "PromptRegistry should keep the same selection for stable repeated input");
}

void test_registry_rejects_unknown_explicit_release() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_registry_missing_release");
  create_prompt_package(temp_directory.path(), PromptPackageOptions{
                                                   .version = "2026.04.11",
                                                   .default_release = true,
                                               });

  auto registry = make_registry(temp_directory.path());
  auto query = make_query();
  query.prompt_release_id = "planner@missing";

  const auto result = registry.select(query);

  assert_true(result.has_consistent_values(),
              "PromptRegistry should return a consistent failure result for unknown explicit release ids");
  assert_true(!result.release.has_value(),
              "PromptRegistry should not fabricate a PromptRelease when explicit release lookup fails");
  assert_true(result.code == ResultCode::ValidationFieldMissing,
              "PromptRegistry should surface missing explicit releases as validation failures");
}

}  // namespace

int main() {
  try {
    test_registry_prefers_explicit_release_over_scene_persona_and_default();
    test_registry_selects_scene_persona_then_profile_then_default();
    test_registry_rejects_unknown_explicit_release();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
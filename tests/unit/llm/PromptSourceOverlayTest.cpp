#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "LLMSubsystemConfig.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/PromptAssetRepository.h"

namespace {

class TempDirectory {
 public:
  explicit TempDirectory(const std::string& prefix) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            (prefix + "_" + std::to_string(unique));
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
                           const std::string& source_label) {
  const std::filesystem::path package_root = root / "planner" / version;
  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "package_id: planner." + version + "\n"
             "prompt_id: planner\n"
             "version: \"2026.04.11\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "default_release: true\n"
             "source_uri: prompt://planner/" + source_label + "\n"
             "tags:\n"
             "  - planner\n"
             "  - " + source_label + "\n");
  write_file(package_root / "system.md", source_label + " system");
  write_file(package_root / "task.md", source_label + " task");
}

void test_repository_prefers_snapshot_over_deployment_over_baseline() {
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptAssetRepository;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempDirectory baseline_root("dasall_prompt_overlay_baseline");
  TempDirectory deployment_root("dasall_prompt_overlay_deployment");
  TempDirectory snapshot_root("dasall_prompt_overlay_snapshot");

  create_prompt_package(baseline_root.path(), "default", "baseline");
  create_prompt_package(deployment_root.path(), "default", "deployment");
  create_prompt_package(snapshot_root.path(), "default", "snapshot");

  PromptAssetRepository repository;
  PromptAssetSourceConfig config;
  config.baseline_root = baseline_root.path().generic_string();
  config.deployment_root = deployment_root.path().generic_string();
  config.snapshot_cache_root = snapshot_root.path().generic_string();
  config.cache_ttl_ms = 60000U;

  assert_true(repository.init(config),
              "PromptAssetRepository should load baseline, deployment, and snapshot layers");
  const auto snapshot = repository.snapshot();
  const auto* descriptor = snapshot->find_release("planner", "2026.04.11");
  assert_true(descriptor != nullptr,
              "overlay prompt catalog should expose the planner 2026.04.11 release");
  assert_equal(std::string("snapshot"), descriptor->source_layer,
               "snapshot layer should override deployment and baseline prompt packages");
  assert_equal(std::string("snapshot system"), *descriptor->release.system_instructions,
               "highest-priority prompt source should control the loaded prompt body");
}

void test_repository_keeps_last_valid_catalog_when_snapshot_turns_invalid() {
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptAssetRepository;
  using dasall::tests::support::assert_true;

  TempDirectory baseline_root("dasall_prompt_overlay_fallback_baseline");
  TempDirectory snapshot_root("dasall_prompt_overlay_fallback_snapshot");

  create_prompt_package(baseline_root.path(), "default", "baseline");
  create_prompt_package(snapshot_root.path(), "default", "snapshot");

  PromptAssetRepository repository;
  PromptAssetSourceConfig config;
  config.baseline_root = baseline_root.path().generic_string();
  config.snapshot_cache_root = snapshot_root.path().generic_string();
  config.cache_ttl_ms = 60000U;

  assert_true(repository.init(config),
              "PromptAssetRepository should initialize successfully with a valid snapshot layer");
  const auto before_reload = repository.snapshot();
  const auto* before_descriptor = before_reload->find_release("planner", "2026.04.11");
  assert_true(before_descriptor != nullptr,
              "valid prompt snapshot should publish a planner release before corruption");
  const std::string previous_hash = before_descriptor->content_hash;

  write_file(snapshot_root.path() / "planner" / "default" / "manifest.yaml",
             "schema_version: \"2\"\n"
             "min_loader_version: \"1\"\n"
             "prompt_id: planner\n"
             "version: \"2026.04.11\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "tags:\n"
             "  - planner\n");

  assert_true(!repository.reload(),
              "PromptAssetRepository should reject corrupted snapshot packages on reload");
  const auto after_reload = repository.snapshot();
  const auto* after_descriptor = after_reload->find_release("planner", "2026.04.11");
  assert_true(after_descriptor != nullptr,
              "PromptAssetRepository should retain the last valid catalog after reload failure");
  assert_true(after_descriptor->content_hash == previous_hash,
              "reload failure should keep the previously published prompt catalog snapshot intact");
  assert_true(repository.last_error_message().find("schema_version") != std::string::npos,
              "reload failure should report the validation reason for the rejected snapshot package");
}

}  // namespace

int main() {
  try {
    test_repository_prefers_snapshot_over_deployment_over_baseline();
    test_repository_keeps_last_valid_catalog_when_snapshot_turns_invalid();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
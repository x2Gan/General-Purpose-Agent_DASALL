#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "LLMSubsystemConfig.h"
#include "prompt/PromptComposeRequest.h"
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
                           const std::string& prompt_id,
                           const std::string& version,
                           const std::string& system_text,
                           const std::string& task_text) {
  const std::filesystem::path package_root = root / prompt_id / version;
  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "package_id: " + prompt_id + "." + version + "\n"
             "prompt_id: " + prompt_id + "\n"
             "version: \"" + version + "\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "model_family: openai_compatible\n"
             "tags:\n"
             "  - planner\n"
             "  - baseline\n");
  write_file(package_root / "system.md", system_text);
  write_file(package_root / "task.md", task_text);
}

void test_repository_loads_repo_baseline_prompt_package() {
  using dasall::contracts::CompositionStage;
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptAssetRepository;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptAssetRepository repository;
  PromptAssetSourceConfig config;
  config.baseline_root =
      (std::filesystem::path(DASALL_REPO_ROOT) / "llm/assets/prompts").generic_string();

  assert_true(repository.init(config),
              "PromptAssetRepository should load the repository baseline prompt assets");
  const auto snapshot = repository.snapshot();
  assert_true(snapshot != nullptr, "PromptAssetRepository should publish a prompt catalog snapshot");
  assert_true(snapshot->has_consistent_values(),
              "published prompt catalog snapshot should satisfy repository invariants");

  const auto* descriptor = snapshot->find_release("planner", "2026.04.11");
  assert_true(descriptor != nullptr,
              "baseline prompt catalog should expose the planner 2026.04.11 release");
  assert_equal(std::string("baseline"), descriptor->source_layer,
               "baseline prompt package should report baseline as its source layer");
  assert_true(descriptor->release.system_instructions->find("DASALL") != std::string::npos,
              "system.md content should be loaded into PromptRelease.system_instructions");
  assert_true(descriptor->release.task_template->find("{{user_goal}}") != std::string::npos,
              "task.md content should be loaded into PromptRelease.task_template");
  assert_true(!descriptor->content_hash.empty(),
              "loaded prompt package should expose a deterministic content hash");

  const auto* perception_descriptor = snapshot->find_release("perception", "2026.05.31");
  assert_true(perception_descriptor != nullptr,
              "baseline prompt catalog should expose the perception 2026.05.31 release");
  assert_true(perception_descriptor->release.stage.has_value(),
              "perception baseline prompt should preserve a parsed stage value");
  assert_true(*perception_descriptor->release.stage == CompositionStage::Perception,
              "perception baseline prompt should parse to the canonical Perception stage");
  assert_true(perception_descriptor->release.task_template->find("perception") != std::string::npos,
              "perception baseline prompt should load the perception task template body");
}

void test_repository_updates_content_hash_when_prompt_body_changes() {
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptAssetRepository;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_asset_package_parse");
  create_prompt_package(temp_directory.path(), "planner", "hash-test", "system-one", "task-one");

  PromptAssetRepository repository;
  PromptAssetSourceConfig config;
  config.baseline_root = temp_directory.path().generic_string();

  assert_true(repository.init(config),
              "PromptAssetRepository should load a valid temporary prompt package");
  const auto before_snapshot = repository.snapshot();
  const auto* before_descriptor = before_snapshot->find_release("planner", "hash-test");
  assert_true(before_descriptor != nullptr,
              "temporary prompt package should be present before the body update");
  const std::string first_hash = before_descriptor->content_hash;

  write_file(temp_directory.path() / "planner" / "hash-test" / "system.md", "system-two");

  assert_true(repository.reload(),
              "PromptAssetRepository should reload the updated prompt package body");
  const auto after_snapshot = repository.snapshot();
  const auto* after_descriptor = after_snapshot->find_release("planner", "hash-test");
  assert_true(after_descriptor != nullptr,
              "temporary prompt package should remain present after reload");
  assert_true(first_hash != after_descriptor->content_hash,
              "content hash should change when the prompt body changes on disk");
}

void test_repository_rejects_prompt_package_with_missing_required_fields() {
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptAssetRepository;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_asset_missing_field");
  const std::filesystem::path package_root = temp_directory.path() / "planner" / "invalid";
  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "version: \"invalid\"\n"
             "stage: planning\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://planner/default\n"
             "trusted_source: profiles\n"
             "tags:\n"
             "  - planner\n");
  write_file(package_root / "system.md", "system-invalid");
  write_file(package_root / "task.md", "task-invalid");

  PromptAssetRepository repository;
  PromptAssetSourceConfig config;
  config.baseline_root = temp_directory.path().generic_string();

  assert_true(!repository.init(config),
              "PromptAssetRepository should reject prompt packages missing prompt_id");
  assert_true(repository.last_error_message().find("prompt_id") != std::string::npos,
              "missing required prompt manifest fields should surface in the repository error message");
}

}  // namespace

int main() {
  try {
    test_repository_loads_repo_baseline_prompt_package();
    test_repository_updates_content_hash_when_prompt_body_changes();
    test_repository_rejects_prompt_package_with_missing_required_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
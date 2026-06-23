#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "LLMSubsystemConfig.h"
#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/PromptRegistry.h"

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
                           const std::string& package_id,
                           const std::string& source_uri,
                           const std::string& system_text,
                           const std::string& task_text) {
  const std::filesystem::path package_root = root / prompt_id / version;
  write_file(package_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "min_loader_version: \"1\"\n"
             "package_id: " + package_id + "\n"
             "prompt_id: " + prompt_id + "\n"
             "version: \"" + version + "\"\n"
             "stage: response\n"
             "eval_status: stable\n"
             "release_scope: stable\n"
             "output_schema_ref: schema://responder/default\n"
             "trusted_source: profiles\n"
             "default_release: true\n"
             "language: zh-cn\n"
             "model_family: openai_compatible\n"
             "source_uri: " + source_uri + "\n"
             "tags:\n"
             "  - responder\n"
             "  - programmatic\n");
  write_file(package_root / "system.md", system_text);
  write_file(package_root / "task.md", task_text);
}

void test_prompt_registry_lookup_release_asset_returns_metadata_fields() {
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptRegistry;
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_asset_metadata_lookup");
  create_prompt_package(temp_directory.path(),
                        "responder",
                        "2026.06.23",
                        "responder.programmatic",
                        "prompt://responder/programmatic",
                        "system prompt body",
                        "task prompt body");

  PromptRegistry registry;
  PromptRegistryConfig config;
  config.asset_sources = PromptAssetSourceConfig{
      .baseline_root = temp_directory.path().generic_string(),
  };
  config.trusted_sources = {"profiles"};

  assert_true(registry.init(config),
              "PromptRegistry should initialize before prompt asset lookup");

  const auto metadata = registry.lookup_release_asset("responder@2026.06.23");
  assert_true(metadata.has_value(),
              "lookup_release_asset should return metadata for a known prompt release");
  assert_equal(std::string("responder@2026.06.23"), metadata->prompt_release_id,
               "lookup_release_asset should preserve the prompt release identifier");
  assert_equal(std::string("responder.programmatic"), metadata->package_id,
               "lookup_release_asset should surface the prompt package id");
  assert_equal(std::string("baseline"), metadata->source_layer,
               "lookup_release_asset should report the effective source layer");
  assert_equal(std::string("prompt://responder/programmatic"), metadata->source_uri,
               "lookup_release_asset should surface the configured source uri");
  assert_true(!metadata->content_hash.empty(),
              "lookup_release_asset should surface a deterministic content digest");
}

void test_prompt_registry_lookup_release_asset_rejects_unknown_or_malformed_release_ids() {
  using dasall::llm::PromptAssetSourceConfig;
  using dasall::llm::prompt::PromptRegistry;
  using dasall::llm::prompt::PromptRegistryConfig;
  using dasall::tests::support::assert_true;

  TempDirectory temp_directory("dasall_prompt_asset_metadata_lookup_invalid");
  create_prompt_package(temp_directory.path(),
                        "responder",
                        "2026.06.23",
                        "responder.programmatic",
                        "prompt://responder/programmatic",
                        "system prompt body",
                        "task prompt body");

  PromptRegistry registry;
  PromptRegistryConfig config;
  config.asset_sources = PromptAssetSourceConfig{
      .baseline_root = temp_directory.path().generic_string(),
  };
  config.trusted_sources = {"profiles"};

  assert_true(registry.init(config),
              "PromptRegistry should initialize before invalid lookup coverage");
  assert_true(!registry.lookup_release_asset("responder").has_value(),
              "lookup_release_asset should reject malformed release ids without a version separator");
  assert_true(!registry.lookup_release_asset("responder@missing").has_value(),
              "lookup_release_asset should reject unknown prompt releases");
}

}  // namespace

int main() {
  try {
    test_prompt_registry_lookup_release_asset_returns_metadata_fields();
    test_prompt_registry_lookup_release_asset_rejects_unknown_or_malformed_release_ids();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
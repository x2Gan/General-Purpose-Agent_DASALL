#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/provider/ProviderCatalogRepository.h"

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

void create_catalog_index(const std::filesystem::path& root, const std::string& source_version) {
  write_file(root / "catalog.yaml",
             "schema_version: \"1\"\n"
             "default_source_version: " + source_version + "\n"
             "packages:\n"
             "  - deepseek\n");
}

void create_provider_package(const std::filesystem::path& root,
                             const std::string& auth_ref,
                             const std::string& base_url_alias,
                             const std::string& header_ref,
                             const std::string& source_version,
                             std::uint32_t context_window) {
  const std::filesystem::path provider_root = root / "deepseek";
  write_file(provider_root / "manifest.yaml",
             "schema_version: \"1\"\n"
             "provider_id: deepseek-prod\n"
             "display_name: DeepSeek Production\n"
             "adapter_family: openai_compatible\n"
             "api_family: openai-completions\n"
             "base_url: https://api.deepseek.com\n"
             "base_url_alias: " + base_url_alias + "\n"
             "auth_mode: bearer_api_key\n"
             "auth_ref: " + auth_ref + "\n"
             "header_refs:\n"
             "  - " + header_ref + "\n"
             "trusted_source: profiles\n"
             "source_version: " + source_version + "\n"
             "activation_flag: true\n"
             "tags:\n"
             "  - cloud\n"
             "  - deepseek\n"
             "mutable_overlay_fields:\n"
             "  - auth_ref\n"
             "  - header_refs\n"
             "  - base_url_alias\n"
             "  - activation_flag\n");
  write_file(provider_root / "models.yaml",
             "schema_version: \"1\"\n"
             "models:\n"
             "  deepseek-chat:\n"
             "    id: deepseek-chat\n"
             "    display_name: DeepSeek Chat\n"
             "    model_version: DeepSeek-V3.2\n"
             "    tier_family: default\n"
             "    latency_tier: low\n"
             "    cost_tier: low\n"
             "    reasoning_depth_tier: standard\n"
             "    aliases:\n"
             "      - deepseek/default\n"
             "    context_window: " + std::to_string(context_window) + "\n"
             "    default_max_output_tokens: 4096\n"
             "    max_output_tokens_hard_limit: 8192\n"
             "    input_modalities:\n"
             "      - text\n"
             "    supports_tools: true\n"
             "    supports_streaming: true\n"
             "    supports_json_object: true\n"
             "    supports_json_schema: false\n"
             "    supports_reasoning: false\n"
             "    supports_visible_reasoning: false\n"
             "    supports_prompt_cache: true\n"
             "    supports_native_stream_usage: false\n"
             "    pricing:\n"
             "      pricing_ref: deepseek-v3.2-official-2026-04-10\n"
             "      input_cache_hit_usd_per_1m: 0.014\n"
             "      input_cache_miss_usd_per_1m: 0.14\n"
             "      output_usd_per_1m: 2.19\n"
             "    metadata_source_uri: https://api-docs.deepseek.com/quick_start/pricing\n"
             "    metadata_effective_at: 2026-04-10\n"
             "    verification_state:\n"
             "      tools: verified\n");
}

void test_repository_applies_mutable_overlay_fields_without_touching_static_model_metadata() {
  using dasall::llm::ProviderCatalogSourceConfig;
  using dasall::llm::provider::ProviderCatalogRepository;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempDirectory baseline_root("dasall_provider_overlay_baseline");
  TempDirectory deployment_root("dasall_provider_overlay_deployment");

  create_catalog_index(baseline_root.path(), "2026.04.10");
  create_catalog_index(deployment_root.path(), "2026.04.11");
  create_provider_package(baseline_root.path(), "secret://llm/providers/deepseek-prod",
                          "deepseek/default", "header://llm/providers/deepseek-org",
                          "2026.04.10", 131072U);
  create_provider_package(deployment_root.path(), "secret://llm/providers/deepseek-canary",
                          "deepseek/canary", "header://llm/providers/deepseek-canary-org",
                          "2026.04.11", 131072U);

  ProviderCatalogRepository repository;
  ProviderCatalogSourceConfig config;
  config.baseline_root = baseline_root.path().generic_string();
  config.deployment_root = deployment_root.path().generic_string();

  assert_true(repository.init(config),
              "ProviderCatalogRepository should accept overlays that only change declared mutable fields");
  const auto snapshot = repository.snapshot();
  const auto* provider = snapshot->find_provider("deepseek-prod");
  const auto* model = snapshot->find_model("deepseek-prod", "deepseek-chat");
  assert_true(provider != nullptr && model != nullptr,
              "overlay provider catalog should still publish the deepseek provider and model entries");
  assert_equal(std::string("secret://llm/providers/deepseek-canary"), provider->descriptor.auth_ref,
               "deployment overlay should be able to override auth_ref");
  assert_equal(std::string("deepseek/canary"), provider->runtime.base_url_alias,
               "deployment overlay should be able to override base_url_alias");
  assert_true(provider->descriptor.header_refs.size() == 1U &&
                  provider->descriptor.header_refs.front() ==
                      "header://llm/providers/deepseek-canary-org",
              "deployment overlay should be able to override header_refs");
  assert_true(model->summary.context_window == 131072U,
              "mutable provider overlays must not rewrite static model context metadata");
}

void test_repository_keeps_last_valid_catalog_when_overlay_changes_immutable_model_metadata() {
  using dasall::llm::ProviderCatalogSourceConfig;
  using dasall::llm::provider::ProviderCatalogRepository;
  using dasall::tests::support::assert_true;

  TempDirectory baseline_root("dasall_provider_overlay_fallback_baseline");
  TempDirectory snapshot_root("dasall_provider_overlay_fallback_snapshot");

  create_catalog_index(baseline_root.path(), "2026.04.10");
  create_catalog_index(snapshot_root.path(), "2026.04.11");
  create_provider_package(baseline_root.path(), "secret://llm/providers/deepseek-prod",
                          "deepseek/default", "header://llm/providers/deepseek-org",
                          "2026.04.10", 131072U);
  create_provider_package(snapshot_root.path(), "secret://llm/providers/deepseek-prod",
                          "deepseek/default", "header://llm/providers/deepseek-org",
                          "2026.04.11", 131072U);

  ProviderCatalogRepository repository;
  ProviderCatalogSourceConfig config;
  config.baseline_root = baseline_root.path().generic_string();
  config.snapshot_cache_root = snapshot_root.path().generic_string();

  assert_true(repository.init(config),
              "ProviderCatalogRepository should initialize with a valid baseline and snapshot");
  const auto before_reload = repository.snapshot();
  const auto* before_model = before_reload->find_model("deepseek-prod", "deepseek-chat");
  assert_true(before_model != nullptr,
              "valid provider snapshot should publish the deepseek-chat model before corruption");

  create_provider_package(snapshot_root.path(), "secret://llm/providers/deepseek-prod",
                          "deepseek/default", "header://llm/providers/deepseek-org",
                          "2026.04.12", 524288U);

  assert_true(!repository.reload(),
              "ProviderCatalogRepository should reject overlays that rewrite immutable model metadata");
  const auto after_reload = repository.snapshot();
  const auto* after_model = after_reload->find_model("deepseek-prod", "deepseek-chat");
  assert_true(after_model != nullptr,
              "ProviderCatalogRepository should retain the last valid catalog after reload failure");
  assert_true(after_model->summary.context_window == before_model->summary.context_window,
              "reload failure should keep the previously published model metadata snapshot intact");
  assert_true(repository.last_error_message().find("immutable model metadata") != std::string::npos,
              "reload failure should report the immutable metadata violation");
}

}  // namespace

int main() {
  try {
    test_repository_applies_mutable_overlay_fields_without_touching_static_model_metadata();
    test_repository_keeps_last_valid_catalog_when_overlay_changes_immutable_model_metadata();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
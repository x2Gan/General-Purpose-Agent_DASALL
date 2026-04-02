#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "config/ConfigErrors.h"
#include "config/ConfigLoader.h"
#include "config/ConfigMerger.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::filesystem::path test_workspace_root() {
  return std::filesystem::temp_directory_path() / "dasall-config-merger-test";
}

void write_text_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

[[nodiscard]] const dasall::infra::config::TypedConfig* find_entry(
    const std::vector<dasall::infra::config::TypedConfig>& data,
    std::string_view key_path) {
  const auto entry = std::find_if(data.begin(), data.end(), [&](const auto& candidate) {
    return candidate.key_path == key_path;
  });
  if (entry == data.end()) {
    return nullptr;
  }

  return &(*entry);
}

[[nodiscard]] dasall::infra::config::ConfigLayerDocument make_layer_document(
    dasall::infra::config::ConfigSourceKind source_kind,
    dasall::infra::config::ConfigDocumentFormat document_format,
    std::string source_id,
    std::string version_ref,
    std::string key_path,
    dasall::infra::config::ConfigValueType value_type,
    std::string serialized_value) {
  return dasall::infra::config::ConfigLayerDocument{
      .layer_ref = dasall::infra::config::ConfigLayerRef{
          .source_kind = source_kind,
          .document_format = document_format,
          .source_id = source_id,
          .version_ref = std::move(version_ref),
          .schema_version = std::string("1"),
      },
      .entries = {dasall::infra::config::TypedConfig{
          .key_path = std::move(key_path),
          .value_type = value_type,
          .serialized_value = std::move(serialized_value),
          .schema_version = std::string("1"),
          .source_kind = source_kind,
          .source_id = std::move(source_id),
          .secret_backed = false,
      }},
  };
}

void test_config_merger_applies_linear_priority_and_records_source_chain() {
  using dasall::infra::config::ConfigDocumentFormat;
  using dasall::infra::config::ConfigLoader;
  using dasall::infra::config::ConfigLoaderOptions;
  using dasall::infra::config::ConfigMerger;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::tests::support::assert_true;

  const std::filesystem::path workspace_root = test_workspace_root();
  std::filesystem::remove_all(workspace_root);

  const std::filesystem::path deploy_path = workspace_root / "deploy" / "site-001.yaml";
  write_text_file(deploy_path,
                  "ops_policy:\n"
                  "  log_level: debug\n"
                  "infra:\n"
                  "  config:\n"
                  "    watch:\n"
                  "      debounce_ms: 750\n");

  const std::filesystem::path runtime_overlay_path = workspace_root / "runtime" / "overlay.yaml";
  write_text_file(runtime_overlay_path,
                  "ops_policy:\n"
                  "  log_level: error\n"
                  "infra:\n"
                  "  config:\n"
                  "    watch:\n"
                  "      enabled: false\n");

  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = runtime_overlay_path.string(),
  });
  ConfigMerger merger;

  const auto defaults = loader.load_default();
  const auto profile = loader.load_profile("desktop_full");
  const auto deploy = loader.load_deploy(deploy_path.string());
  const auto runtime_overlay = loader.load_runtime_overlay();
  assert_true(defaults.loaded && profile.loaded && deploy.loaded && runtime_overlay.loaded,
              "ConfigMerger test setup requires all four layers to load successfully");

  const auto merged = merger.merge(
      {defaults.document, profile.document, deploy.document, runtime_overlay.document});
  assert_true(merged.merged && merged.snapshot.is_valid(),
              "ConfigMerger should produce a valid snapshot when four ordered layers are provided");
  assert_true(merged.snapshot.source_chain.size() == 4U &&
                  merged.snapshot.source_chain[0].source_kind == ConfigSourceKind::Defaults &&
                  merged.snapshot.source_chain[1].source_kind == ConfigSourceKind::Profile &&
                  merged.snapshot.source_chain[2].source_kind == ConfigSourceKind::DeploymentOverride &&
                  merged.snapshot.source_chain[3].source_kind == ConfigSourceKind::RuntimeOverride,
              "ConfigMerger should preserve the ordered source_chain for defaults/profile/deploy/runtime layers");

  const auto* log_level_entry = find_entry(merged.snapshot.data, "ops_policy.log_level");
  assert_true(log_level_entry != nullptr && log_level_entry->serialized_value == "error" &&
                  log_level_entry->source_kind == ConfigSourceKind::RuntimeOverride,
              "ConfigMerger should let later layers override earlier ones and retain the winning source metadata");

  const auto* profile_id_entry = find_entry(merged.snapshot.data, "profile_meta.profile_id");
  assert_true(profile_id_entry != nullptr && profile_id_entry->serialized_value == "desktop_full" &&
                  profile_id_entry->source_kind == ConfigSourceKind::Profile,
              "ConfigMerger should keep profile-only keys in the merged snapshot data");

  const auto* debounce_entry = find_entry(merged.snapshot.data, "infra.config.watch.debounce_ms");
  assert_true(debounce_entry != nullptr && debounce_entry->serialized_value == "750" &&
                  debounce_entry->source_kind == ConfigSourceKind::DeploymentOverride,
              "ConfigMerger should retain deployment overrides for keys not replaced by runtime overlay");

  std::filesystem::remove_all(workspace_root);
}

void test_config_merger_reports_locatable_conflicts_for_type_mismatches() {
  using dasall::infra::config::ConfigDocumentFormat;
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigMerger;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigMerger merger;
  const auto merged = merger.merge({
      make_layer_document(ConfigSourceKind::Defaults,
                          ConfigDocumentFormat::RuntimePolicyYamlV1,
                          std::string("infra/config/defaults/runtime_policy.yaml"),
                          std::string("defaults@1"),
                          std::string("ops_policy.log_level"),
                          ConfigValueType::String,
                          std::string("warn")),
      make_layer_document(ConfigSourceKind::Profile,
                          ConfigDocumentFormat::RuntimePolicyYamlV1,
                          std::string("profiles/desktop_full/runtime_policy.yaml"),
                          std::string("desktop_full@1"),
                          std::string("ops_policy.log_level"),
                          ConfigValueType::Boolean,
                          std::string("true")),
  });

  assert_true(!merged.merged && merged.references_only_contract_error_types(),
              "ConfigMerger should keep conflict failures inside the frozen contracts error envelope");
  assert_true(merged.result_code == map_config_error_code(ConfigErrorCode::Conflict).result_code,
              "ConfigMerger should map merge conflicts to the frozen config conflict category");
  assert_true(merged.error_info.has_value() &&
                  merged.error_info->details.message.find("ops_policy.log_level") != std::string::npos,
              "ConfigMerger should report the conflicting key_path so the override conflict is locatable");
}

}  // namespace

int main() {
  try {
    test_config_merger_applies_linear_priority_and_records_source_chain();
    test_config_merger_reports_locatable_conflicts_for_type_mismatches();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
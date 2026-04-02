#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "config/ConfigErrors.h"
#include "config/ConfigLoader.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::filesystem::path test_workspace_root() {
  return std::filesystem::temp_directory_path() / "dasall-config-loader-test";
}

void write_text_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << content;
}

[[nodiscard]] const dasall::infra::config::TypedConfig* find_entry(
    const dasall::infra::config::ConfigLayerDocument& document,
    std::string_view key_path) {
  const auto entry = std::find_if(document.entries.begin(),
                                  document.entries.end(),
                                  [&](const dasall::infra::config::TypedConfig& candidate) {
                                    return candidate.key_path == key_path;
                                  });
  if (entry == document.entries.end()) {
    return nullptr;
  }

  return &(*entry);
}

void test_config_loader_reads_four_layers_with_frozen_source_ids_and_versions() {
  using dasall::infra::config::ConfigDocumentFormat;
  using dasall::infra::config::ConfigLoader;
  using dasall::infra::config::ConfigLoaderOptions;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::tests::support::assert_true;

  const std::filesystem::path workspace_root = test_workspace_root();
  std::filesystem::remove_all(workspace_root);

  const std::filesystem::path deploy_path = workspace_root / "deploy" / "site-001.yaml";
  write_text_file(deploy_path,
                  "ops_policy:\n"
                  "  log_level: debug\n"
                  "runtime_budget:\n"
                  "  max_turns: 18\n");

  const std::filesystem::path runtime_overlay_path = workspace_root / "runtime" / "overlay.yaml";
  write_text_file(runtime_overlay_path,
                  "ops_policy:\n"
                  "  trace_sample_ratio: 0.5\n"
                  "enabled_modules:\n"
                  "  infra_observability: false\n");

  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = runtime_overlay_path.string(),
  });

  const auto defaults = loader.load_default();
  assert_true(defaults.loaded && defaults.document.is_valid(),
              "ConfigLoader should expose a valid defaults layer document");
  assert_true(defaults.document.layer_ref.source_kind == ConfigSourceKind::Defaults &&
                  defaults.document.layer_ref.document_format == ConfigDocumentFormat::RuntimePolicyYamlV1 &&
                  defaults.document.layer_ref.source_id == "infra/config/defaults/runtime_policy.yaml" &&
                  defaults.document.layer_ref.version_ref == "defaults@1",
              "ConfigLoader should freeze defaults source_id and version_ref for the first layer");
  const auto* default_entry = find_entry(defaults.document, "ops_policy.log_level");
  assert_true(default_entry != nullptr && default_entry->serialized_value == "warn",
              "ConfigLoader should expose built-in defaults entries while repo-scoped defaults assets are not frozen");

  const auto profile = loader.load_profile("desktop_full");
  assert_true(profile.loaded && profile.document.is_valid(),
              "ConfigLoader should read the frozen desktop_full runtime policy asset");
  assert_true(profile.document.layer_ref.source_kind == ConfigSourceKind::Profile &&
                  profile.document.layer_ref.document_format == ConfigDocumentFormat::RuntimePolicyYamlV1 &&
                  profile.document.layer_ref.source_id == "profiles/desktop_full/runtime_policy.yaml" &&
                  profile.document.layer_ref.version_ref == "desktop_full@1",
              "ConfigLoader should emit the frozen profile source_id and version_ref");
  const auto* profile_entry = find_entry(profile.document, "profile_meta.profile_id");
  assert_true(profile_entry != nullptr && profile_entry->serialized_value == "desktop_full",
              "ConfigLoader should flatten runtime_policy.yaml into typed key paths");

  const auto deploy = loader.load_deploy(deploy_path.string());
  assert_true(deploy.loaded && deploy.document.is_valid(),
              "ConfigLoader should read a local deployment overlay source file");
  assert_true(deploy.document.layer_ref.source_kind == ConfigSourceKind::DeploymentOverride &&
                  deploy.document.layer_ref.document_format == ConfigDocumentFormat::DeploymentOverlayYamlV1 &&
                  deploy.document.layer_ref.version_ref == "deploy@1",
              "ConfigLoader should keep deployment overrides in the third layer position with a stable version tag");
  const auto* deploy_entry = find_entry(deploy.document, "ops_policy.log_level");
  assert_true(deploy_entry != nullptr && deploy_entry->serialized_value == "debug",
              "ConfigLoader should flatten deployment overlay yaml into typed config entries");

  const auto runtime_overlay = loader.load_runtime_overlay();
  assert_true(runtime_overlay.loaded && runtime_overlay.document.is_valid(),
              "ConfigLoader should read a local runtime overlay source file");
  assert_true(runtime_overlay.document.layer_ref.source_kind == ConfigSourceKind::RuntimeOverride &&
                  runtime_overlay.document.layer_ref.document_format ==
                      ConfigDocumentFormat::RuntimeOverridePatchV1 &&
                  runtime_overlay.document.layer_ref.version_ref == "runtime-overlay@1",
              "ConfigLoader should freeze the runtime overlay version tag for the fourth layer");
  const auto* runtime_entry = find_entry(runtime_overlay.document, "enabled_modules.infra_observability");
  assert_true(runtime_entry != nullptr && runtime_entry->serialized_value == "false",
              "ConfigLoader should flatten runtime overlay data into typed config entries");

  std::filesystem::remove_all(workspace_root);
}

void test_config_loader_rejects_invalid_profile_and_missing_managed_sources() {
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigLoader;
  using dasall::infra::config::ConfigLoaderOptions;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = std::string(),
  });

  const auto invalid_profile = loader.load_profile("staging");
  assert_true(!invalid_profile.loaded && invalid_profile.references_only_contract_error_types(),
              "ConfigLoader should reject unfrozen profile aliases and keep failures inside contracts error types");
  assert_true(invalid_profile.result_code ==
                  map_config_error_code(ConfigErrorCode::InvalidSchema).result_code,
              "ConfigLoader should map unsupported profile aliases to the frozen config validation category");

  const auto missing_deploy = loader.load_deploy("deploy/missing.yaml");
  assert_true(!missing_deploy.loaded && missing_deploy.references_only_contract_error_types(),
              "ConfigLoader should reject unreachable deployment overlay sources");
  assert_true(missing_deploy.result_code ==
                  map_config_error_code(ConfigErrorCode::SourceUnavailable).result_code,
              "ConfigLoader should map deployment source lookup failures to the frozen provider category");

  const auto missing_runtime_overlay = loader.load_runtime_overlay();
  assert_true(!missing_runtime_overlay.loaded && missing_runtime_overlay.references_only_contract_error_types(),
              "ConfigLoader should reject runtime overlay loads when no managed source file is configured");
  assert_true(missing_runtime_overlay.result_code ==
                  map_config_error_code(ConfigErrorCode::SourceUnavailable).result_code,
              "ConfigLoader should map runtime overlay lookup failures to the frozen provider category");
}

}  // namespace

int main() {
  try {
    test_config_loader_reads_four_layers_with_frozen_source_ids_and_versions();
    test_config_loader_rejects_invalid_profile_and_missing_managed_sources();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
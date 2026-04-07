#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "config/ConfigLoader.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::infra::config::ConfigLoader;
using dasall::infra::config::ConfigLoaderOptions;
using dasall::infra::config::ConfigSourceKind;
using dasall::infra::config::ConfigValueType;
using dasall::infra::config::TypedConfig;

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] const TypedConfig* find_entry(
    const dasall::infra::config::ConfigLayerDocument& document,
    std::string_view key_path) {
  const auto entry = std::find_if(document.entries.begin(),
                                  document.entries.end(),
                                  [&](const TypedConfig& candidate) {
                                    return candidate.key_path == key_path;
                                  });
  if (entry == document.entries.end()) {
    return nullptr;
  }

  return &(*entry);
}

void assert_entry_equals(const dasall::infra::config::ConfigLayerDocument& document,
                         std::string_view key_path,
                         ConfigValueType expected_type,
                         std::string_view expected_value,
                         std::string_view expected_source_id,
                         std::string_view message) {
  using dasall::tests::support::assert_true;

  const TypedConfig* entry = find_entry(document, key_path);
  assert_true(entry != nullptr,
              std::string(message) + " missing key_path=" + std::string(key_path));
  assert_true(entry->value_type == expected_type,
              std::string(message) + " should keep expected typed config kind");
  assert_true(entry->serialized_value == expected_value,
              std::string(message) + " should keep expected serialized value");
  assert_true(entry->source_kind == ConfigSourceKind::Profile &&
                  entry->source_id == expected_source_id,
              std::string(message) + " should keep profile-scoped provenance");
}

void test_profile_plugin_matrix_keeps_profile_specific_limits_and_allowlists() {
  using dasall::tests::support::assert_true;

  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = std::string(),
  });

  const auto desktop = loader.load_profile("desktop_full");
  const auto balanced = loader.load_profile("edge_balanced");
  const auto minimal = loader.load_profile("edge_minimal");

  assert_true(desktop.loaded && desktop.document.is_valid(),
              "desktop_full plugin matrix integration should load the frozen desktop profile runtime policy");
  assert_true(balanced.loaded && balanced.document.is_valid(),
              "edge_balanced plugin matrix integration should load the frozen balanced profile runtime policy");
  assert_true(minimal.loaded && minimal.document.is_valid(),
              "edge_minimal plugin matrix integration should load the frozen minimal profile runtime policy");

  assert_entry_equals(desktop.document,
                      "infra.plugin.allowlist",
                      ConfigValueType::StringList,
                      "[plugin.echo,plugin.metrics,plugin.tools.bridge]",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full plugin allowlist");
  assert_entry_equals(desktop.document,
                      "infra.plugin.search_paths",
                      ConfigValueType::StringList,
                      "[./plugins,./plugins/desktop]",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full plugin search paths");
  assert_entry_equals(desktop.document,
                      "infra.plugin.load_timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "3000",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full plugin load timeout");
  assert_entry_equals(desktop.document,
                      "infra.plugin.max_active",
                      ConfigValueType::UnsignedInteger,
                      "16",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full plugin max_active");
  assert_entry_equals(desktop.document,
                      "infra.plugin.safe_mode.fail_threshold",
                      ConfigValueType::UnsignedInteger,
                      "3",
                      "profiles/desktop_full/runtime_policy.yaml",
                      "desktop_full plugin safe mode threshold");

  assert_entry_equals(balanced.document,
                      "infra.plugin.allowlist",
                      ConfigValueType::StringList,
                      "[plugin.echo,plugin.edge.telemetry]",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced plugin allowlist");
  assert_entry_equals(balanced.document,
                      "infra.plugin.search_paths",
                      ConfigValueType::StringList,
                      "[./plugins,./plugins/edge]",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced plugin search paths");
  assert_entry_equals(balanced.document,
                      "infra.plugin.load_timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "2500",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced plugin load timeout");
  assert_entry_equals(balanced.document,
                      "infra.plugin.max_active",
                      ConfigValueType::UnsignedInteger,
                      "8",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced plugin max_active");
  assert_entry_equals(balanced.document,
                      "infra.plugin.safe_mode.fail_threshold",
                      ConfigValueType::UnsignedInteger,
                      "2",
                      "profiles/edge_balanced/runtime_policy.yaml",
                      "edge_balanced plugin safe mode threshold");

  assert_entry_equals(minimal.document,
                      "infra.plugin.allowlist",
                      ConfigValueType::StringList,
                      "[plugin.echo]",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal plugin allowlist");
  assert_entry_equals(minimal.document,
                      "infra.plugin.search_paths",
                      ConfigValueType::StringList,
                      "[./plugins,./plugins/minimal]",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal plugin search paths");
  assert_entry_equals(minimal.document,
                      "infra.plugin.load_timeout_ms",
                      ConfigValueType::UnsignedInteger,
                      "1800",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal plugin load timeout");
  assert_entry_equals(minimal.document,
                      "infra.plugin.max_active",
                      ConfigValueType::UnsignedInteger,
                      "4",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal plugin max_active");
  assert_entry_equals(minimal.document,
                      "infra.plugin.safe_mode.fail_threshold",
                      ConfigValueType::UnsignedInteger,
                      "1",
                      "profiles/edge_minimal/runtime_policy.yaml",
                      "edge_minimal plugin safe mode threshold");
}

void test_profile_plugin_matrix_keeps_security_baselines_aligned_across_profiles() {
  using dasall::tests::support::assert_true;

  ConfigLoader loader(ConfigLoaderOptions{
      .repository_root = repository_root(),
      .runtime_overlay_source_ref = std::string(),
  });

  const auto desktop = loader.load_profile("desktop_full");
  const auto balanced = loader.load_profile("edge_balanced");
  const auto minimal = loader.load_profile("edge_minimal");

  assert_true(desktop.loaded && balanced.loaded && minimal.loaded,
              "profile plugin matrix integration should load all three frozen profile assets before comparing security baselines");

  for (const auto* document : {&desktop.document, &balanced.document, &minimal.document}) {
    const TypedConfig* signature_required = find_entry(*document, "infra.plugin.signature.required");
    const TypedConfig* strict_mode = find_entry(*document, "infra.plugin.abi.strict_mode");
    const TypedConfig* remote_fetch = find_entry(*document, "infra.plugin.remote_fetch.enabled");
    const TypedConfig* trust_level = find_entry(*document, "infra.plugin.trust.min_level");
    const TypedConfig* enabled = find_entry(*document, "infra.plugin.enabled");

    assert_true(signature_required != nullptr &&
                    signature_required->value_type == ConfigValueType::Boolean &&
                    signature_required->serialized_value == "true",
                "all plugin profile matrices must keep signature verification enabled");
    assert_true(strict_mode != nullptr && strict_mode->value_type == ConfigValueType::Boolean &&
                    strict_mode->serialized_value == "true",
                "all plugin profile matrices must keep ABI strict mode enabled in the baseline");
    assert_true(remote_fetch != nullptr && remote_fetch->value_type == ConfigValueType::Boolean &&
                    remote_fetch->serialized_value == "false",
                "all plugin profile matrices must keep remote fetch disabled in the baseline");
    assert_true(trust_level != nullptr && trust_level->value_type == ConfigValueType::String &&
                    trust_level->serialized_value == "internal",
                "all plugin profile matrices must keep the minimum trust level frozen at internal");
    assert_true(enabled != nullptr && enabled->value_type == ConfigValueType::Boolean &&
                    enabled->serialized_value == "true",
                "all plugin profile matrices must keep plugin governance enabled in the baseline");
  }
}

}  // namespace

int main() {
  try {
    test_profile_plugin_matrix_keeps_profile_specific_limits_and_allowlists();
    test_profile_plugin_matrix_keeps_security_baselines_aligned_across_profiles();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
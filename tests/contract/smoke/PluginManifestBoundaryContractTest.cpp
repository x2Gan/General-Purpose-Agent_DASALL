#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "plugin/PluginManifest.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasRequestId = requires(T value) { value.request_id; };

template <typename T>
concept HasTraceId = requires(T value) { value.trace_id; };

template <typename T>
concept HasTaskId = requires(T value) { value.task_id; };

template <typename T>
concept HasToolId = requires(T value) { value.tool_id; };

template <typename T>
concept HasSkillId = requires(T value) { value.skill_id; };

void test_plugin_manifest_stays_inside_plugin_local_schema_surface() {
  using dasall::infra::plugin::PluginManifest;
  using dasall::infra::plugin::PluginManifestExtension;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginManifest{}.schema_version), std::string>);
  static_assert(std::is_same_v<decltype(PluginManifest{}.plugin_id), std::string>);
  static_assert(std::is_same_v<decltype(PluginManifest{}.version), std::string>);
  static_assert(std::is_same_v<decltype(PluginManifest{}.entry), std::string>);
  static_assert(std::is_same_v<decltype(PluginManifest{}.required_abi), std::string>);
  static_assert(std::is_same_v<decltype(PluginManifest{}.capabilities), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PluginManifest{}.signature_ref), std::string>);
  static_assert(
      std::is_same_v<decltype(PluginManifest{}.extensions), std::vector<PluginManifestExtension>>);
  static_assert(!HasRequestId<PluginManifest>);
  static_assert(!HasTraceId<PluginManifest>);
  static_assert(!HasTaskId<PluginManifest>);
  static_assert(!HasToolId<PluginManifest>);
  static_assert(!HasSkillId<PluginManifest>);

  const auto manifest = PluginManifest::normalize(PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.2.3"),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::string("x86_64-linux-gnu@1.2.0"),
      .capabilities = {std::string("plugin.echo.execute")},
      .signature_ref = std::string("sig:plugin.echo@1.2.3"),
      .extensions = {PluginManifestExtension{
          .key = std::string("x.acme.debug_hint"),
          .serialized_value = std::string("disabled"),
      }},
  });
  assert_true(manifest.is_valid(),
              "plugin manifest should remain representable without contracts/tool/skill-only identifiers");

  const PluginManifest reserved_extension_manifest = PluginManifest::normalize(PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.2.3"),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::string("x86_64-linux-gnu@1.2.0"),
      .capabilities = {std::string("plugin.echo.execute")},
      .signature_ref = std::string("sig:plugin.echo@1.2.3"),
      .extensions = {PluginManifestExtension{
          .key = std::string("x.tool.shadow"),
          .serialized_value = std::string("true"),
      }},
  });
  assert_true(!reserved_extension_manifest.is_valid(),
              "plugin manifest must reject reserved extension owners that could shadow tool/skill or runtime semantics");
}

}  // namespace

int main() {
  try {
    test_plugin_manifest_stays_inside_plugin_local_schema_surface();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
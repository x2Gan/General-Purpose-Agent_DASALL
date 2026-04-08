#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "plugin/IPluginCompatibilityEngine.h"
#include "support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasRuntimeBridgeHandle : std::false_type {};

template <typename T>
struct HasRuntimeBridgeHandle<T, std::void_t<decltype(std::declval<T>().runtime_bridge_handle)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasPolicySnapshot : std::false_type {};

template <typename T>
struct HasPolicySnapshot<T, std::void_t<decltype(std::declval<T>().policy_snapshot)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasErrorInfo : std::false_type {};

template <typename T>
struct HasErrorInfo<T, std::void_t<decltype(std::declval<T>().error_info)>>
    : std::true_type {};

void test_plugin_compatibility_engine_request_keeps_manifest_host_abi_and_dependency_matrix_boundary() {
  using dasall::infra::plugin::CompatibilityReport;
  using dasall::infra::plugin::IPluginCompatibilityEngine;
  using dasall::infra::plugin::PluginCompatibilityCheckRequest;
  using dasall::infra::plugin::PluginDependencyMatrixSnapshot;
  using dasall::infra::plugin::PluginHostAbiSnapshot;
  using dasall::infra::plugin::PluginManifest;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginCompatibilityCheckRequest{}.manifest), PluginManifest>);
  static_assert(std::is_same_v<decltype(PluginCompatibilityCheckRequest{}.host_abi), PluginHostAbiSnapshot>);
  static_assert(std::is_same_v<decltype(PluginCompatibilityCheckRequest{}.dependency_matrix), PluginDependencyMatrixSnapshot>);
  static_assert(std::is_same_v<decltype(PluginHostAbiSnapshot{}.platform_tag), std::string>);
  static_assert(std::is_same_v<decltype(PluginHostAbiSnapshot{}.abi_version), std::string>);
  static_assert(std::is_same_v<decltype(PluginDependencyMatrixSnapshot{}.required_dependency_refs), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PluginDependencyMatrixSnapshot{}.available_dependency_refs), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(std::declval<const IPluginCompatibilityEngine&>().check(
                                   std::declval<const PluginCompatibilityCheckRequest&>())),
                               CompatibilityReport>);
  static_assert(!HasRuntimeBridgeHandle<PluginCompatibilityCheckRequest>::value);
  static_assert(!HasPolicySnapshot<PluginCompatibilityCheckRequest>::value);
  static_assert(!HasErrorInfo<CompatibilityReport>::value);

  const auto manifest = dasall::infra::plugin::PluginManifest::normalize(dasall::infra::plugin::PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::string("plugin.echo.vendor"),
      .version = std::string("1.2.3"),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::string("x86_64-linux-gnu@1.2.0"),
      .capabilities = {std::string("plugin.echo.execute")},
      .signature_ref = std::string("sig:plugin.echo.vendor@1.2.3"),
      .extensions = {dasall::infra::plugin::PluginManifestExtension{
          .key = std::string("x.acme.runtime_profile"),
          .serialized_value = std::string("desktop"),
      }},
  });
  const PluginCompatibilityCheckRequest request{
      .manifest = manifest,
      .host_abi = PluginHostAbiSnapshot{
          .platform_tag = std::string("x86_64-linux-gnu"),
          .abi_version = std::string("1.2.3"),
          .strict_mode = true,
          .api_ready = true,
      },
      .dependency_matrix = PluginDependencyMatrixSnapshot{
          .required_dependency_refs = {std::string("plugin.core.runtime")},
          .available_dependency_refs = {std::string("plugin.core.runtime"),
                                        std::string("plugin.core.metrics")},
      },
  };

  assert_true(request.is_valid(),
              "plugin compatibility engine request should stay representable as manifest plus host ABI/dependency snapshots without runtime or policy internals");

  auto invalid_host_abi = request.host_abi;
  invalid_host_abi.platform_tag = std::string("darwin-arm64");
  assert_true(!invalid_host_abi.is_valid(),
              "plugin compatibility engine should reject platform tags outside the frozen GNU triplet allow-list");

  auto invalid_dependency_matrix = request.dependency_matrix;
  invalid_dependency_matrix.available_dependency_refs = {std::string("plugin.core.runtime"),
                                                         std::string("plugin.core.runtime")};
  assert_true(!invalid_dependency_matrix.is_valid(),
              "plugin compatibility engine should reject duplicate dependency refs in the frozen dependency snapshot boundary");
}

}  // namespace

int main() {
  try {
    test_plugin_compatibility_engine_request_keeps_manifest_host_abi_and_dependency_matrix_boundary();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <string>

#include "plugin/PluginManifest.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::plugin::PluginManifest make_valid_manifest() {
  using dasall::infra::plugin::PluginManifest;
  using dasall::infra::plugin::PluginManifestExtension;

  return PluginManifest::normalize(PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.2.3"),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::string("x86_64-linux-gnu@1.2.0"),
      .capabilities = {std::string("plugin.echo.execute"), std::string("plugin.echo.observe")},
      .signature_ref = std::string("sig:plugin.echo@1.2.3"),
      .extensions = {PluginManifestExtension{
          .key = std::string("x.acme.debug_hint"),
          .serialized_value = std::string("disabled"),
      }},
  });
}

void test_plugin_manifest_defaults_to_unknown_and_stays_invalid() {
  using dasall::infra::plugin::PluginManifest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const PluginManifest manifest;

  assert_true(manifest.uses_unknown_defaults(),
              "plugin manifest should default every frozen field to unknown or empty");
  assert_equal(std::string("unknown"), manifest.schema_version,
               "schema_version should default to unknown before schema freeze");
  assert_equal(std::string("unknown"), manifest.plugin_id,
               "plugin_id should default to unknown before manifest binding");
  assert_true(!manifest.is_valid(),
              "default manifest should not pass the frozen schema contract");
}

void test_plugin_manifest_accepts_frozen_v1_schema_shape() {
  using dasall::tests::support::assert_true;

  const auto manifest = make_valid_manifest();

  assert_true(manifest.is_schema_frozen_v1(),
              "manifest schema should recognize the frozen v1.0.0 identifier");
  assert_true(manifest.has_valid_capabilities(),
              "manifest capabilities should stay unique and non-empty");
  assert_true(manifest.has_valid_extensions(),
              "manifest extensions should stay inside the allowed x.<owner> namespace");
  assert_true(manifest.is_valid(),
              "manifest with frozen schema, ABI, signature_ref and capabilities should validate");
}

void test_plugin_manifest_rejects_reserved_extension_namespace_and_malformed_required_abi() {
  using dasall::tests::support::assert_true;

  auto invalid_extension = make_valid_manifest();
  invalid_extension.extensions = {
      dasall::infra::plugin::PluginManifestExtension{
          .key = std::string("x.runtime.shadow"),
          .serialized_value = std::string("true"),
      }};
  assert_true(!invalid_extension.has_valid_extensions(),
              "reserved owner namespaces must be rejected even under the x.<owner> pattern");
  assert_true(!invalid_extension.is_valid(),
              "manifest should reject extension keys that shadow runtime/plugin reserved domains");

  auto invalid_required_abi = make_valid_manifest();
  invalid_required_abi.required_abi = std::string("linux.gcc13");
  assert_true(!invalid_required_abi.is_valid(),
              "manifest should reject required_abi values that are not encoded as <platform>@<semver>");
}

}  // namespace

int main() {
  try {
    test_plugin_manifest_defaults_to_unknown_and_stays_invalid();
    test_plugin_manifest_accepts_frozen_v1_schema_shape();
    test_plugin_manifest_rejects_reserved_extension_namespace_and_malformed_required_abi();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
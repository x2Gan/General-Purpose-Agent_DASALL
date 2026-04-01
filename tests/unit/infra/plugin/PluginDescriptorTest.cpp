#include <exception>
#include <iostream>
#include <string>

#include "plugin/PluginDescriptor.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_plugin_descriptor_defaults_to_unknown_governance_fields() {
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const PluginDescriptor descriptor;

  assert_true(descriptor.uses_unknown_defaults(),
              "plugin descriptor should default every governance field to unknown");
  assert_equal(std::string("unknown"), descriptor.plugin_id,
               "plugin id should default to unknown");
  assert_equal(std::string("unknown"), descriptor.version,
               "plugin version should default to unknown");
  assert_equal(std::string("unknown"), descriptor.abi,
               "plugin ABI should default to unknown");
  assert_equal(static_cast<int>(PluginTrustLevel::Unknown),
               static_cast<int>(descriptor.trust_level),
               "plugin trust level should default to unknown");
  assert_equal(static_cast<int>(PluginStatus::Unknown),
               static_cast<int>(descriptor.status),
               "plugin status should default to unknown");
  assert_equal(std::string("unknown"), descriptor.source,
               "plugin source should default to unknown");
}

void test_plugin_descriptor_normalizes_empty_fields_to_unknown() {
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const PluginDescriptor normalized = PluginDescriptor::normalize(PluginDescriptor{
      .plugin_id = std::string(),
      .version = std::string(),
      .abi = std::string(),
      .trust_level = PluginTrustLevel::Vendor,
      .status = PluginStatus::Discovered,
      .source = std::string(),
  });

  assert_equal(std::string("unknown"), normalized.plugin_id,
               "empty plugin id should normalize to unknown");
  assert_equal(std::string("unknown"), normalized.version,
               "empty plugin version should normalize to unknown");
  assert_equal(std::string("unknown"), normalized.abi,
               "empty plugin ABI should normalize to unknown");
  assert_equal(std::string("unknown"), normalized.source,
               "empty plugin source should normalize to unknown");
  assert_true(!normalized.is_governance_ready(),
              "descriptor with unknown governance fields must stay not ready");
}

void test_plugin_descriptor_accepts_frozen_governance_shape() {
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::tests::support::assert_true;

  const PluginDescriptor descriptor = PluginDescriptor::normalize(PluginDescriptor{
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.0.0"),
      .abi = std::string("linux.gcc13"),
      .trust_level = PluginTrustLevel::Internal,
      .status = PluginStatus::Validated,
      .source = std::string("./plugins/plugin.echo"),
  });

  assert_true(descriptor.is_governance_ready(),
              "descriptor with all frozen governance fields should become ready");
}

}  // namespace

int main() {
  try {
    test_plugin_descriptor_defaults_to_unknown_governance_fields();
    test_plugin_descriptor_normalizes_empty_fields_to_unknown();
    test_plugin_descriptor_accepts_frozen_governance_shape();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <string>

#include "plugin/PluginCatalog.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::plugin::PluginDescriptor make_descriptor(std::string plugin_id) {
  return dasall::infra::plugin::PluginDescriptor::normalize(
      dasall::infra::plugin::PluginDescriptor{
          .plugin_id = std::move(plugin_id),
          .version = std::string("1.0.0"),
          .abi = std::string("linux.gcc13"),
          .trust_level = dasall::infra::plugin::PluginTrustLevel::Internal,
          .status = dasall::infra::plugin::PluginStatus::Discovered,
          .source = std::string("./plugins/manifest.json"),
      });
}

dasall::infra::plugin::RejectedPluginRecord make_rejected_plugin(std::string plugin_id,
                                                                 std::string reason_code,
                                                                 std::string evidence_ref) {
  return dasall::infra::plugin::RejectedPluginRecord{
      .descriptor = make_descriptor(std::move(plugin_id)),
      .reason_code = std::move(reason_code),
      .evidence_ref = std::move(evidence_ref),
  };
}

void test_plugin_catalog_accepts_empty_catalog() {
  using dasall::infra::plugin::PluginCatalog;
  using dasall::tests::support::assert_true;

  const PluginCatalog catalog;

  assert_true(catalog.empty(), "plugin catalog should allow the empty discovery result");
  assert_true(catalog.has_consistent_entries(),
              "empty plugin catalog should still be internally consistent");
}

void test_plugin_catalog_accepts_all_discovered_plugins() {
  using dasall::infra::plugin::PluginCatalog;
  using dasall::tests::support::assert_true;

  const PluginCatalog catalog{
      .discovered_plugins = {make_descriptor("plugin.echo"), make_descriptor("plugin.trace")},
      .rejected_plugins = {},
  };

  assert_true(!catalog.empty(), "catalog with discovered entries should not be empty");
  assert_true(catalog.has_consistent_entries(),
              "catalog should accept a discovered-only result set");
}

void test_plugin_catalog_accepts_all_rejected_plugins_when_reasons_are_traceable() {
  using dasall::infra::plugin::PluginCatalog;
  using dasall::tests::support::assert_true;

  const PluginCatalog catalog{
      .discovered_plugins = {},
      .rejected_plugins = {
          make_rejected_plugin("plugin.echo", "plugin_signature_missing", "observation:plugin.echo"),
          make_rejected_plugin("plugin.trace", "plugin_policy_denied", "observation:plugin.trace"),
      },
  };

  assert_true(!catalog.empty(), "catalog with rejected entries should not be empty");
  assert_true(catalog.has_traceable_rejections(),
              "rejected plugins must keep reason and evidence references");
  assert_true(catalog.has_consistent_entries(),
              "catalog should accept an all-rejected result set when rejections stay traceable");
}

void test_plugin_catalog_rejects_untraceable_or_duplicate_entries() {
  using dasall::infra::plugin::PluginCatalog;
  using dasall::tests::support::assert_true;

  const PluginCatalog missing_evidence{
      .discovered_plugins = {},
      .rejected_plugins = {
          make_rejected_plugin("plugin.echo", "plugin_signature_missing", std::string()),
      },
  };

  const PluginCatalog duplicate_discovered{
      .discovered_plugins = {make_descriptor("plugin.echo"), make_descriptor("plugin.echo")},
      .rejected_plugins = {},
  };

  assert_true(!missing_evidence.has_traceable_rejections(),
              "rejected plugin entries without evidence_ref must be rejected");
  assert_true(!missing_evidence.has_consistent_entries(),
              "catalog must reject untraceable rejected entries");
  assert_true(!duplicate_discovered.has_consistent_entries(),
              "catalog must reject duplicate discovered plugin identifiers");
}

}  // namespace

int main() {
  try {
    test_plugin_catalog_accepts_empty_catalog();
    test_plugin_catalog_accepts_all_discovered_plugins();
    test_plugin_catalog_accepts_all_rejected_plugins_when_reasons_are_traceable();
    test_plugin_catalog_rejects_untraceable_or_duplicate_entries();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
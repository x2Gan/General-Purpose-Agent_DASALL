#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "plugin/PluginCatalog.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasObservation = requires(T value) { value.observation; };

template <typename T>
concept HasObservationDigest = requires(T value) { value.observation_digest; };

template <typename T>
concept HasErrorInfo = requires(T value) { value.error_info; };

void test_plugin_catalog_reuses_only_evidence_refs_at_contract_boundary() {
  using dasall::infra::plugin::PluginCatalog;
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::infra::plugin::RejectedPluginRecord;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginCatalog{}.discovered_plugins),
                               std::vector<PluginDescriptor>>);
  static_assert(std::is_same_v<decltype(PluginCatalog{}.rejected_plugins),
                               std::vector<RejectedPluginRecord>>);
  static_assert(std::is_same_v<decltype(RejectedPluginRecord{}.reason_code), std::string>);
  static_assert(std::is_same_v<decltype(RejectedPluginRecord{}.evidence_ref), std::string>);
  static_assert(!HasObservation<RejectedPluginRecord>);
  static_assert(!HasObservationDigest<RejectedPluginRecord>);
  static_assert(!HasErrorInfo<RejectedPluginRecord>);

  const PluginCatalog catalog{
      .discovered_plugins = {},
      .rejected_plugins = {RejectedPluginRecord{
          .descriptor = PluginDescriptor::normalize(PluginDescriptor{
              .plugin_id = std::string("plugin.echo"),
              .version = std::string("1.0.0"),
              .abi = std::string("linux.gcc13"),
              .trust_level = PluginTrustLevel::External,
              .status = PluginStatus::Rejected,
              .source = std::string("./plugins/plugin.echo"),
          }),
          .reason_code = std::string("plugin_policy_denied"),
          .evidence_ref = std::string("observation:plugin.echo"),
      }},
  };

  assert_true(catalog.has_traceable_rejections(),
              "plugin catalog rejections should align to evidence_ref strings rather than owning observations");
}

}  // namespace

int main() {
  try {
    test_plugin_catalog_reuses_only_evidence_refs_at_contract_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
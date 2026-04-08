#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "plugin/PluginDescriptor.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasRequestId = requires(T value) { value.request_id; };

template <typename T>
concept HasTraceId = requires(T value) { value.trace_id; };

template <typename T>
concept HasTaskId = requires(T value) { value.task_id; };

void test_plugin_descriptor_stays_inside_plugin_local_governance_surface() {
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginDescriptor{}.plugin_id), std::string>);
  static_assert(std::is_same_v<decltype(PluginDescriptor{}.version), std::string>);
  static_assert(std::is_same_v<decltype(PluginDescriptor{}.abi), std::string>);
  static_assert(std::is_same_v<decltype(PluginDescriptor{}.source), std::string>);
  static_assert(std::is_same_v<decltype(PluginDescriptor{}.trust_level), PluginTrustLevel>);
  static_assert(std::is_same_v<decltype(PluginDescriptor{}.status), PluginStatus>);
  static_assert(!HasRequestId<PluginDescriptor>);
  static_assert(!HasTraceId<PluginDescriptor>);
  static_assert(!HasTaskId<PluginDescriptor>);

  const PluginDescriptor descriptor = PluginDescriptor::normalize(PluginDescriptor{
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.0.0"),
      .abi = std::string("linux.gcc13"),
      .trust_level = PluginTrustLevel::External,
      .status = PluginStatus::Discovered,
      .source = std::string("./plugins/plugin.echo"),
  });

  assert_true(descriptor.is_governance_ready(),
              "plugin descriptor should stay representable without contracts-only identifiers");
}

}  // namespace

int main() {
  try {
    test_plugin_descriptor_stays_inside_plugin_local_governance_surface();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
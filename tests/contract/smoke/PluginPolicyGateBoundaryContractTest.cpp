#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "plugin/IPluginPolicyGate.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasManifest : std::false_type {};

template <typename T>
struct HasManifest<T, std::void_t<decltype(std::declval<T>().manifest)>> : std::true_type {};

template <typename T, typename = void>
struct HasPolicyBundle : std::false_type {};

template <typename T>
struct HasPolicyBundle<T, std::void_t<decltype(std::declval<T>().policy_bundle)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasErrorInfo : std::false_type {};

template <typename T>
struct HasErrorInfo<T, std::void_t<decltype(std::declval<T>().error_info)>> : std::true_type {};

void test_plugin_policy_gate_request_keeps_descriptor_and_ref_only_boundary() {
  using dasall::infra::plugin::IPluginPolicyGate;
  using dasall::infra::plugin::PluginDescriptor;
  using dasall::infra::plugin::PluginPolicyRequest;
  using dasall::infra::policy::PolicyDecisionRef;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginPolicyRequest{}.descriptor), PluginDescriptor>);
  static_assert(std::is_same_v<decltype(PluginPolicyRequest{}.manifest_ref), std::string>);
  static_assert(std::is_same_v<decltype(PluginPolicyRequest{}.profile_id), std::string>);
  static_assert(!HasManifest<PluginPolicyRequest>::value);
  static_assert(!HasPolicyBundle<PluginPolicyRequest>::value);
  static_assert(!HasErrorInfo<PluginPolicyRequest>::value);
  static_assert(std::is_same_v<decltype(std::declval<const IPluginPolicyGate&>().evaluate(
                                   std::declval<const PluginPolicyRequest&>(),
                                   std::declval<const PolicySnapshot&>())),
                               PolicyDecisionRef>);

  const PluginPolicyRequest request{
      .descriptor = dasall::infra::plugin::PluginDescriptor::normalize(dasall::infra::plugin::PluginDescriptor{
          .plugin_id = std::string("plugin.echo"),
          .version = std::string("1.0.0"),
          .abi = std::string("linux.gcc13"),
          .trust_level = dasall::infra::plugin::PluginTrustLevel::Internal,
          .status = dasall::infra::plugin::PluginStatus::Discovered,
          .source = std::string("./plugins/plugin.echo"),
      }),
      .manifest_ref = std::string("manifest:plugin.echo@1"),
      .profile_id = std::string("desktop_full"),
  };

  assert_true(request.is_valid(),
              "plugin policy gate request should freeze PluginDescriptor plus manifest/profile refs without owning blocked manifest or policy objects");
}

}  // namespace

int main() {
  try {
    test_plugin_policy_gate_request_keeps_descriptor_and_ref_only_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
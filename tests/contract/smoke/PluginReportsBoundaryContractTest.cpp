#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "plugin/PluginReports.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasErrorInfo : std::false_type {};

template <typename T>
struct HasErrorInfo<T, std::void_t<decltype(std::declval<T>().error_info)>> : std::true_type {};

template <typename T, typename = void>
struct HasPolicyDecision : std::false_type {};

template <typename T>
struct HasPolicyDecision<T, std::void_t<decltype(std::declval<T>().policy_decision)>>
    : std::true_type {};

template <typename T>
concept HasRequestId = requires(T value) { value.request_id; };

template <typename T>
concept HasTraceId = requires(T value) { value.trace_id; };

template <typename T>
concept HasTaskId = requires(T value) { value.task_id; };

void test_plugin_reports_stay_inside_plugin_public_boundary() {
  using dasall::infra::plugin::CompatibilityReport;
  using dasall::infra::plugin::PluginSignatureChainStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::infra::plugin::SignatureReport;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(SignatureReport{}.verified), bool>);
  static_assert(std::is_same_v<decltype(SignatureReport{}.signer), std::string>);
  static_assert(std::is_same_v<decltype(SignatureReport{}.algorithm), std::string>);
  static_assert(std::is_same_v<decltype(SignatureReport{}.chain_status), PluginSignatureChainStatus>);
  static_assert(std::is_same_v<decltype(SignatureReport{}.inferred_trust_level), PluginTrustLevel>);
  static_assert(std::is_same_v<decltype(SignatureReport{}.reason_code), std::string>);
  static_assert(std::is_same_v<decltype(SignatureReport{}.evidence_ref), std::string>);
  static_assert(std::is_same_v<decltype(CompatibilityReport{}.reason_codes), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(CompatibilityReport{}.resolved_platform_tag), std::string>);
  static_assert(std::is_same_v<decltype(CompatibilityReport{}.required_abi), std::string>);
  static_assert(!HasErrorInfo<SignatureReport>::value);
  static_assert(!HasErrorInfo<CompatibilityReport>::value);
  static_assert(!HasPolicyDecision<SignatureReport>::value);
  static_assert(!HasPolicyDecision<CompatibilityReport>::value);
  static_assert(!HasRequestId<SignatureReport>);
  static_assert(!HasRequestId<CompatibilityReport>);
  static_assert(!HasTraceId<SignatureReport>);
  static_assert(!HasTraceId<CompatibilityReport>);
  static_assert(!HasTaskId<SignatureReport>);
  static_assert(!HasTaskId<CompatibilityReport>);

  const auto signature_report = SignatureReport::success(
      std::string("signer:plugin.echo"),
      std::string("ed25519"),
      PluginTrustLevel::Vendor,
      std::string("evidence://signature/plugin.echo/verified"),
      std::string("plugin_signature_verified"));
  const auto compatibility_report = CompatibilityReport::failure(
      false,
      true,
      false,
      {std::string("plugin_abi_incompatible")},
      std::string("x86_64-linux-gnu"),
      std::string("x86_64-linux-gnu@1.2.0"),
      std::string("evidence://compat/plugin.echo/fail"));

  assert_true(signature_report.is_valid() && compatibility_report.is_valid(),
              "shared plugin reports should stay representable without leaking contracts or policy-only payloads");
  assert_equal(std::string("verified"),
               std::string(dasall::infra::plugin::plugin_signature_chain_status_name(
                   PluginSignatureChainStatus::Verified)),
               "shared report boundary should preserve the verified chain status token");
}

}  // namespace

int main() {
  try {
    test_plugin_reports_stay_inside_plugin_public_boundary();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
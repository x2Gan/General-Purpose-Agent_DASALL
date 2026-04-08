#include <exception>
#include <iostream>
#include <string>

#include "plugin/PluginReports.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_signature_report_freezes_chain_status_names_and_algorithm_allow_list() {
  using dasall::infra::plugin::PluginSignatureChainStatus;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  assert_equal(std::string("verified"),
               std::string(dasall::infra::plugin::plugin_signature_chain_status_name(
                   PluginSignatureChainStatus::Verified)),
               "signature report should keep the verified chain status token frozen");
  assert_equal(std::string("rollback_rejected"),
               std::string(dasall::infra::plugin::plugin_signature_chain_status_name(
                   PluginSignatureChainStatus::RollbackRejected)),
               "signature report should keep the rollback rejection token frozen");
  assert_true(dasall::infra::plugin::is_plugin_signature_algorithm_allowed("ed25519") &&
                  dasall::infra::plugin::is_plugin_signature_algorithm_allowed(
                      "ecdsa-p256-sha256") &&
                  !dasall::infra::plugin::is_plugin_signature_algorithm_allowed("rsa-4096"),
              "shared signature report boundary should preserve the two-algorithm allow list");
}

void test_signature_report_validates_success_and_failure_shapes() {
  using dasall::infra::plugin::PluginSignatureChainStatus;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::infra::plugin::SignatureReport;
  using dasall::tests::support::assert_true;

  const SignatureReport success_report = SignatureReport::success(
      std::string("signer:plugin.echo"),
      std::string("ed25519"),
      PluginTrustLevel::Vendor,
      std::string("evidence://signature/plugin.echo/verified"),
      std::string("plugin_signature_verified"));
  assert_true(success_report.is_valid(),
              "signature report success shape should remain valid once promoted to the shared public header");

  const SignatureReport failure_report = SignatureReport::failure(
      std::string("unknown-signature-algorithm"),
      PluginSignatureChainStatus::AlgorithmUnsupported,
      PluginTrustLevel::External,
      std::string("plugin_signature_algorithm_unsupported"),
      std::string("evidence://signature/plugin.echo/unsupported"));
  assert_true(failure_report.is_valid(),
              "signature report failure shape should allow unsupported algorithms only when the chain status is frozen as algorithm_unsupported");

  assert_true(!SignatureReport{}.is_valid(),
              "default signature report should remain invalid until every frozen field is populated");
}

void test_compatibility_report_requires_reason_codes_only_on_failure() {
  using dasall::infra::plugin::CompatibilityReport;
  using dasall::tests::support::assert_true;

  const CompatibilityReport success_report = CompatibilityReport::success(
      std::string("x86_64-linux-gnu"),
      std::string("x86_64-linux-gnu@1.2.0"),
      std::string("evidence://compat/plugin.echo/pass"));
  assert_true(success_report.is_valid(),
              "compatibility report success shape should validate once promoted into the shared public header");

  const CompatibilityReport failure_report = CompatibilityReport::failure(
      false,
      true,
      false,
      {std::string("plugin_abi_incompatible"), std::string("plugin_dependency_missing")},
      std::string("x86_64-linux-gnu"),
      std::string("x86_64-linux-gnu@1.2.0"),
      std::string("evidence://compat/plugin.echo/fail"));
  assert_true(failure_report.is_valid(),
              "compatibility report failure shape should require explicit reason codes when any compatibility gate fails");

  const CompatibilityReport invalid_failure = CompatibilityReport::failure(
      false,
      true,
      false,
      {},
      std::string("x86_64-linux-gnu"),
      std::string("x86_64-linux-gnu@1.2.0"),
      std::string("evidence://compat/plugin.echo/malformed"));
  assert_true(!invalid_failure.is_valid(),
              "compatibility report failures should remain invalid when reason codes are missing");
}

}  // namespace

int main() {
  try {
    test_signature_report_freezes_chain_status_names_and_algorithm_allow_list();
    test_signature_report_validates_success_and_failure_shapes();
    test_compatibility_report_requires_reason_codes_only_on_failure();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
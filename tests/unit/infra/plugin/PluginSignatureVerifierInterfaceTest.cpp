#include <exception>
#include <iostream>
#include <string>
#include <tuple>
#include <type_traits>

#include "plugin/IPluginSignatureVerifier.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

std::tuple<int, int, int> parse_semver_core(std::string_view value) {
  const auto prerelease = value.find('-');
  const auto build = value.find('+');
  const auto core_end = std::min(prerelease == std::string_view::npos ? value.size() : prerelease,
                                 build == std::string_view::npos ? value.size() : build);
  const auto core = value.substr(0, core_end);
  const auto first = core.find('.');
  const auto second = core.find('.', first + 1);

  return {std::stoi(std::string(core.substr(0, first))),
          std::stoi(std::string(core.substr(first + 1, second - first - 1))),
          std::stoi(std::string(core.substr(second + 1)))};
}

bool semver_less_than(std::string_view left, std::string_view right) {
  return parse_semver_core(left) < parse_semver_core(right);
}

dasall::infra::plugin::PluginManifest make_valid_manifest(
    std::string plugin_id = "plugin.echo.vendor",
    std::string version = "1.2.3") {
  using dasall::infra::plugin::PluginManifest;
  using dasall::infra::plugin::PluginManifestExtension;

  return PluginManifest::normalize(PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::move(plugin_id),
      .version = std::move(version),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::string("x86_64-linux-gnu@1.2.0"),
      .capabilities = {std::string("plugin.echo.execute")},
      .signature_ref = std::string("sig:plugin.echo@1.2.3"),
      .extensions = {PluginManifestExtension{
          .key = std::string("x.acme.signature_profile"),
          .serialized_value = std::string("vendor"),
      }},
  });
}

dasall::infra::plugin::PluginTrustAnchorMaterial make_trust_anchor(
    dasall::infra::plugin::PluginTrustLevel minimum_level =
        dasall::infra::plugin::PluginTrustLevel::External,
    std::string last_known_good_version = "1.2.0") {
  return dasall::infra::plugin::PluginTrustAnchorMaterial{
      .anchor_ref = std::string("secret://plugin/root"),
      .anchor_purpose = std::string(dasall::infra::plugin::kPluginSignatureAnchorPurpose),
      .allowed_algorithms = {std::string("ed25519"), std::string("ecdsa-p256-sha256")},
      .minimum_trust_level = minimum_level,
      .last_known_good_version = std::move(last_known_good_version),
  };
}

class NullPluginSignatureVerifier final : public dasall::infra::plugin::IPluginSignatureVerifier {
 public:
  [[nodiscard]] dasall::infra::plugin::SignatureReport verify(
      const dasall::infra::plugin::PluginSignatureVerificationRequest& request) const override {
    using dasall::infra::plugin::PluginSignatureChainStatus;
    using dasall::infra::plugin::PluginTrustLevel;
    using dasall::infra::plugin::SignatureReport;

    if (!request.is_valid()) {
      return SignatureReport::failure(
          request.signature_algorithm,
          PluginSignatureChainStatus::AnchorMissing,
          PluginTrustLevel::Unknown,
          std::string("plugin_signature_request_invalid"),
          std::string("audit:plugin.signature.invalid"));
    }

    if (!dasall::infra::plugin::is_plugin_signature_algorithm_allowed(
            request.signature_algorithm)) {
      return SignatureReport::failure(
          request.signature_algorithm,
          PluginSignatureChainStatus::AlgorithmUnsupported,
          PluginTrustLevel::Unknown,
          std::string("plugin_signature_algorithm_unsupported"),
          std::string("audit:plugin.signature.algorithm_unsupported"));
    }

    if (request.trust_anchor.last_known_good_version != dasall::infra::plugin::kPluginUnknownValue &&
        semver_less_than(request.manifest.version, request.trust_anchor.last_known_good_version)) {
      return SignatureReport::failure(
          request.signature_algorithm,
          PluginSignatureChainStatus::RollbackRejected,
          PluginTrustLevel::Vendor,
          std::string("plugin_signature_rollback_rejected"),
          std::string("audit:plugin.signature.rollback_rejected"),
          std::string("signer://plugin/acme"));
    }

    const auto inferred_trust_level =
        request.manifest.plugin_id == std::string("plugin.echo.internal")
            ? PluginTrustLevel::Internal
            : PluginTrustLevel::Vendor;
    if (!dasall::infra::plugin::plugin_trust_level_meets_minimum(
            inferred_trust_level, request.trust_anchor.minimum_trust_level)) {
      return SignatureReport::failure(
          request.signature_algorithm,
          PluginSignatureChainStatus::TrustLevelTooLow,
          inferred_trust_level,
          std::string("plugin_signature_trust_level_too_low"),
          std::string("audit:plugin.signature.trust_level_too_low"),
          std::string("signer://plugin/acme"));
    }

    return SignatureReport::success(
        std::string("signer://plugin/acme"),
        request.signature_algorithm,
        inferred_trust_level,
        std::string("audit:plugin.signature.verified"));
  }
};

void test_plugin_signature_verifier_interface_accepts_frozen_request_boundary() {
  using dasall::infra::plugin::IPluginSignatureVerifier;
  using dasall::infra::plugin::PluginSignatureVerificationRequest;
  using dasall::infra::plugin::SignatureReport;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<const IPluginSignatureVerifier&>().verify(
                                   std::declval<const PluginSignatureVerificationRequest&>())),
                               SignatureReport>);

  NullPluginSignatureVerifier verifier;
  const auto report = verifier.verify(dasall::infra::plugin::PluginSignatureVerificationRequest{
      .manifest = make_valid_manifest(),
      .package_ref = std::string("package:plugin.echo.vendor@1.2.3"),
      .signature_algorithm = std::string("ed25519"),
      .trust_anchor = make_trust_anchor(),
  });

  assert_true(report.verified && report.is_valid(),
              "IPluginSignatureVerifier should accept frozen manifest and trust anchor inputs and emit a valid signature report");
  assert_true(report.chain_status == dasall::infra::plugin::PluginSignatureChainStatus::Verified,
              "successful verification should freeze chain_status=verified");
}

void test_plugin_signature_verifier_interface_rejects_unsupported_algorithms_and_rollbacks() {
  using dasall::infra::plugin::PluginSignatureChainStatus;
  using dasall::tests::support::assert_true;

  NullPluginSignatureVerifier verifier;

  const auto unsupported_algorithm = verifier.verify(
      dasall::infra::plugin::PluginSignatureVerificationRequest{
          .manifest = make_valid_manifest(),
          .package_ref = std::string("package:plugin.echo.vendor@1.2.3"),
          .signature_algorithm = std::string("sha1"),
          .trust_anchor = make_trust_anchor(),
      });
  assert_true(!unsupported_algorithm.verified && unsupported_algorithm.is_valid(),
              "unsupported algorithms should be rejected through a valid observable signature report");
  assert_true(unsupported_algorithm.chain_status ==
                  PluginSignatureChainStatus::AlgorithmUnsupported,
              "unsupported algorithms should freeze chain_status=algorithm_unsupported");

  const auto rollback_rejected = verifier.verify(
      dasall::infra::plugin::PluginSignatureVerificationRequest{
          .manifest = make_valid_manifest("plugin.echo.vendor", "1.1.9"),
          .package_ref = std::string("package:plugin.echo.vendor@1.1.9"),
          .signature_algorithm = std::string("ecdsa-p256-sha256"),
          .trust_anchor = make_trust_anchor(dasall::infra::plugin::PluginTrustLevel::Vendor,
                                            "1.2.0"),
      });
  assert_true(!rollback_rejected.verified && rollback_rejected.is_valid(),
              "older plugin metadata versions should be rejected through rollback_rejected");
  assert_true(rollback_rejected.chain_status == PluginSignatureChainStatus::RollbackRejected,
              "rollback regressions should freeze chain_status=rollback_rejected");
}

}  // namespace

int main() {
  try {
    test_plugin_signature_verifier_interface_accepts_frozen_request_boundary();
    test_plugin_signature_verifier_interface_rejects_unsupported_algorithms_and_rollbacks();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
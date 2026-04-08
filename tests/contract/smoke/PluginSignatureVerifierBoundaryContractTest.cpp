#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "plugin/IPluginSignatureVerifier.h"
#include "support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasCertificateChain : std::false_type {};

template <typename T>
struct HasCertificateChain<T, std::void_t<decltype(std::declval<T>().certificate_chain)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasPrivateKey : std::false_type {};

template <typename T>
struct HasPrivateKey<T, std::void_t<decltype(std::declval<T>().private_key)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasRawSignatureBlob : std::false_type {};

template <typename T>
struct HasRawSignatureBlob<T, std::void_t<decltype(std::declval<T>().raw_signature_blob)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasErrorInfo : std::false_type {};

template <typename T>
struct HasErrorInfo<T, std::void_t<decltype(std::declval<T>().error_info)>>
    : std::true_type {};

void test_plugin_signature_verifier_request_keeps_manifest_and_anchor_refs_without_crypto_blobs() {
  using dasall::infra::plugin::IPluginSignatureVerifier;
  using dasall::infra::plugin::PluginManifest;
  using dasall::infra::plugin::PluginSignatureVerificationRequest;
  using dasall::infra::plugin::PluginTrustAnchorMaterial;
  using dasall::infra::plugin::PluginTrustLevel;
  using dasall::infra::plugin::SignatureReport;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginSignatureVerificationRequest{}.manifest), PluginManifest>);
  static_assert(std::is_same_v<decltype(PluginSignatureVerificationRequest{}.package_ref), std::string>);
  static_assert(std::is_same_v<decltype(PluginSignatureVerificationRequest{}.signature_algorithm), std::string>);
  static_assert(std::is_same_v<decltype(PluginSignatureVerificationRequest{}.trust_anchor), PluginTrustAnchorMaterial>);
  static_assert(std::is_same_v<decltype(PluginTrustAnchorMaterial{}.anchor_ref), std::string>);
  static_assert(std::is_same_v<decltype(PluginTrustAnchorMaterial{}.anchor_purpose), std::string>);
  static_assert(std::is_same_v<decltype(PluginTrustAnchorMaterial{}.allowed_algorithms), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PluginTrustAnchorMaterial{}.minimum_trust_level), PluginTrustLevel>);
  static_assert(std::is_same_v<decltype(PluginTrustAnchorMaterial{}.last_known_good_version), std::string>);
  static_assert(std::is_same_v<decltype(std::declval<const IPluginSignatureVerifier&>().verify(
                                   std::declval<const PluginSignatureVerificationRequest&>())),
                               SignatureReport>);
  static_assert(!HasCertificateChain<PluginTrustAnchorMaterial>::value);
  static_assert(!HasPrivateKey<PluginTrustAnchorMaterial>::value);
  static_assert(!HasRawSignatureBlob<PluginSignatureVerificationRequest>::value);
  static_assert(!HasErrorInfo<SignatureReport>::value);

  const auto manifest = dasall::infra::plugin::PluginManifest::normalize(dasall::infra::plugin::PluginManifest{
      .schema_version = std::string("1.0.0"),
      .plugin_id = std::string("plugin.echo.vendor"),
      .version = std::string("1.2.3"),
      .entry = std::string("dasall_plugin_entry_v1"),
      .required_abi = std::string("x86_64-linux-gnu@1.2.0"),
      .capabilities = {std::string("plugin.echo.execute")},
      .signature_ref = std::string("sig:plugin.echo.vendor@1.2.3"),
      .extensions = {dasall::infra::plugin::PluginManifestExtension{
          .key = std::string("x.acme.signature_profile"),
          .serialized_value = std::string("vendor"),
      }},
  });
  const PluginTrustAnchorMaterial trust_anchor{
      .anchor_ref = std::string("secret://plugin/root"),
      .anchor_purpose = std::string(dasall::infra::plugin::kPluginSignatureAnchorPurpose),
      .allowed_algorithms = {std::string("ed25519"), std::string("ecdsa-p256-sha256")},
      .minimum_trust_level = PluginTrustLevel::Vendor,
      .last_known_good_version = std::string("1.2.0"),
  };
  const PluginSignatureVerificationRequest request{
      .manifest = manifest,
      .package_ref = std::string("package:plugin.echo.vendor@1.2.3"),
      .signature_algorithm = std::string("ed25519"),
      .trust_anchor = trust_anchor,
  };

  assert_true(request.is_valid(),
              "plugin signature verifier request should remain representable as manifest plus package/anchor refs without raw cryptographic blobs");

  auto invalid_anchor = trust_anchor;
  invalid_anchor.anchor_purpose = std::string("plugin.package.load");
  assert_true(!invalid_anchor.is_valid(),
              "plugin signature verifier should freeze trust anchor purpose to plugin.package.verify");

  auto invalid_algorithm_set = trust_anchor;
  invalid_algorithm_set.allowed_algorithms = {std::string("sha1")};
  assert_true(!invalid_algorithm_set.is_valid(),
              "plugin signature verifier should reject trust stores that escape the frozen algorithm allow-list");
}

}  // namespace

int main() {
  try {
    test_plugin_signature_verifier_request_keeps_manifest_and_anchor_refs_without_crypto_blobs();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
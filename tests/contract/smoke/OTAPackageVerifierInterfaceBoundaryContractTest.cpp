#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "ota/IOTAPackageVerifier.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_ota_package_verifier_interface_keeps_contract_error_types_and_private_success_payloads() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::ArtifactVerificationResult;
  using dasall::infra::ota::IOTAPackageVerifier;
  using dasall::infra::ota::PackageVerificationResult;

  static_assert(std::is_same_v<decltype(std::declval<const IOTAPackageVerifier&>().verify_package(
                                   std::declval<const dasall::infra::ota::PackageDescriptor&>())),
                               PackageVerificationResult>);
  static_assert(std::is_same_v<decltype(std::declval<const IOTAPackageVerifier&>().verify_artifact(
                                   std::declval<const dasall::infra::ota::ArtifactDescriptor&>())),
                               ArtifactVerificationResult>);
  static_assert(std::is_same_v<decltype(PackageVerificationResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(PackageVerificationResult{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(ArtifactVerificationResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(ArtifactVerificationResult{}.result_code), ResultCode>);
}

void test_ota_package_verifier_boundary_results_keep_failure_observability_inside_contracts() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  const auto package_failure = dasall::infra::ota::PackageVerificationResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("signature algorithm mismatch"),
      std::string("ota.verify_package"),
      std::string("BoundaryVerifier"));
  assert_true(package_failure.references_only_contract_error_types(),
              "package verification failure should remain in the frozen contracts ResultCode/ErrorInfo boundary");

  const auto artifact_failure = dasall::infra::ota::ArtifactVerificationResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("artifact descriptor incomplete"),
      std::string("ota.verify_artifact"),
      std::string("BoundaryVerifier"));
  assert_true(artifact_failure.references_only_contract_error_types(),
              "artifact verification failure should remain in the frozen contracts ResultCode/ErrorInfo boundary");

  const auto package_success = dasall::infra::ota::PackageVerificationResult::success(
      dasall::infra::ota::VerifiedPackageManifest{
          .package_id = std::string("pkg-ota-003"),
          .signature_ok = true,
          .hash_set = {std::string("sha256:pkg-003")},
          .release_counter = 12,
          .compatible_profiles = {std::string("desktop_full")},
          .artifact_list = {dasall::infra::ota::ArtifactDescriptor{
              .artifact_id = std::string("artifact-rootfs-a"),
              .artifact_class = dasall::infra::ota::ArtifactClass::SlotBound,
              .target_slot_group = std::string("rootfs"),
              .version = std::string("1.2.3"),
              .hardware_selector = {std::string("board-a")},
              .dependency_refs = {},
          }},
      });
  assert_true(package_success.verified && package_success.references_only_contract_error_types(),
              "successful package verification should keep VerifiedPackageManifest as an infra-private payload");
}

}  // namespace

int main() {
  try {
    test_ota_package_verifier_interface_keeps_contract_error_types_and_private_success_payloads();
    test_ota_package_verifier_boundary_results_keep_failure_observability_inside_contracts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
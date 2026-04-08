#include <exception>
#include <iostream>
#include <string>

#include "ota/ArtifactCompatibilityEvaluator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ota::ArtifactClass;
using dasall::infra::ota::ArtifactCompatibilityEvaluator;
using dasall::infra::ota::ArtifactCompatibilityProfile;
using dasall::infra::ota::ArtifactDescriptor;
using dasall::infra::ota::DeviceCapabilitySnapshot;
using dasall::infra::ota::VerifiedPackageManifest;

ArtifactDescriptor make_valid_artifact_descriptor() {
  return ArtifactDescriptor{
      .artifact_id = std::string("artifact-rootfs-c"),
      .artifact_class = ArtifactClass::SlotBound,
      .target_slot_group = std::string("rootfs"),
      .version = std::string("1.2.8"),
      .hardware_selector = {std::string("board-a")},
      .dependency_refs = {std::string("basefs")},
  };
}

VerifiedPackageManifest make_valid_verified_manifest() {
  return VerifiedPackageManifest{
      .package_id = std::string("pkg-ota-008"),
      .signature_ok = true,
      .hash_set = {std::string("sha256:pkg-008")},
      .release_counter = 13,
      .compatible_profiles = {std::string("desktop_full"), std::string("edge_balanced")},
      .artifact_list = {make_valid_artifact_descriptor()},
  };
}

void test_artifact_compatibility_evaluator_accepts_matching_manifest_capability_and_profile() {
  using dasall::tests::support::assert_true;

  ArtifactCompatibilityEvaluator evaluator;
  const auto report = evaluator.evaluate(
      make_valid_verified_manifest(),
      DeviceCapabilitySnapshot{
          .supported_hardware = {std::string("board-a"), std::string("board-b")},
          .available_dependency_refs = {std::string("basefs")},
      },
      ArtifactCompatibilityProfile{
          .profile_name = std::string("desktop_full"),
          .slot_bound_allowed = true,
          .repo_bound_allowed = true,
          .forbidden_dependency_refs = {},
      });

  assert_true(report.is_valid() && report.compatible &&
                  report.accepted_artifact_ids.size() == 1U,
              "ArtifactCompatibilityEvaluator should accept artifacts only when manifest profile, hardware selector, and dependency refs all match the active device state");
}

void test_artifact_compatibility_evaluator_rejects_hardware_profile_and_dependency_conflicts() {
  using dasall::tests::support::assert_true;

  ArtifactCompatibilityEvaluator evaluator;

  const auto hardware_conflict = evaluator.evaluate(
      make_valid_verified_manifest(),
      DeviceCapabilitySnapshot{
          .supported_hardware = {std::string("board-z")},
          .available_dependency_refs = {std::string("basefs")},
      },
      ArtifactCompatibilityProfile{
          .profile_name = std::string("desktop_full"),
          .slot_bound_allowed = true,
          .repo_bound_allowed = true,
          .forbidden_dependency_refs = {},
      });
  assert_true(hardware_conflict.is_valid() && !hardware_conflict.compatible &&
                  hardware_conflict.uses_contract_error_types_only(),
              "ArtifactCompatibilityEvaluator should reject artifacts whose hardware selector does not intersect device capabilities");

  const auto profile_conflict = evaluator.evaluate(
      make_valid_verified_manifest(),
      DeviceCapabilitySnapshot{
          .supported_hardware = {std::string("board-a")},
          .available_dependency_refs = {std::string("basefs")},
      },
      ArtifactCompatibilityProfile{
          .profile_name = std::string("edge_minimal"),
          .slot_bound_allowed = false,
          .repo_bound_allowed = true,
          .forbidden_dependency_refs = {},
      });
  assert_true(profile_conflict.is_valid() && !profile_conflict.compatible &&
                  profile_conflict.uses_contract_error_types_only(),
              "ArtifactCompatibilityEvaluator should reject manifests that do not allow the active profile or disable slot_bound artifacts for it");

  const auto dependency_conflict = evaluator.evaluate(
      make_valid_verified_manifest(),
      DeviceCapabilitySnapshot{
          .supported_hardware = {std::string("board-a")},
          .available_dependency_refs = {},
      },
      ArtifactCompatibilityProfile{
          .profile_name = std::string("desktop_full"),
          .slot_bound_allowed = true,
          .repo_bound_allowed = true,
          .forbidden_dependency_refs = {std::string("basefs")},
      });
  assert_true(dependency_conflict.is_valid() && !dependency_conflict.compatible &&
                  dependency_conflict.uses_contract_error_types_only(),
              "ArtifactCompatibilityEvaluator should reject artifacts whose dependency_refs are forbidden or unavailable on the device");
}

}  // namespace

int main() {
  try {
    test_artifact_compatibility_evaluator_accepts_matching_manifest_capability_and_profile();
    test_artifact_compatibility_evaluator_rejects_hardware_profile_and_dependency_conflicts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
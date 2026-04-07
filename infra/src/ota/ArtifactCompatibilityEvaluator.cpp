#include "ota/ArtifactCompatibilityEvaluator.h"

#include <algorithm>
#include <string>
#include <utility>

namespace dasall::infra::ota {
namespace {

constexpr char kArtifactCompatibilitySourceRef[] = "ArtifactCompatibilityEvaluator";

[[nodiscard]] contracts::ErrorInfo make_error_info(std::string message,
                                                   std::string stage) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(
          contracts::ResultCode::PolicyDenied),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(contracts::ResultCode::PolicyDenied),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "infra.ota",
          .ref_id = kArtifactCompatibilitySourceRef,
      },
  };
}

[[nodiscard]] contracts::ErrorInfo make_validation_error(std::string message,
                                                         std::string stage) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(
          contracts::ResultCode::ValidationFieldMissing),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "infra.ota",
          .ref_id = kArtifactCompatibilitySourceRef,
      },
  };
}

[[nodiscard]] bool contains(const std::vector<std::string>& values,
                            const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] bool intersects(const std::vector<std::string>& lhs,
                              const std::vector<std::string>& rhs) {
  return std::any_of(lhs.begin(), lhs.end(), [&](const std::string& value) {
    return contains(rhs, value);
  });
}

}  // namespace

CompatibilityReport ArtifactCompatibilityEvaluator::evaluate(
    const VerifiedPackageManifest& verified_manifest,
    const DeviceCapabilitySnapshot& capability,
    const ArtifactCompatibilityProfile& profile) const {
  CompatibilityReport report{
      .compatible = false,
      .accepted_artifact_ids = {},
      .blocking_reasons = {},
  };

  if (!verified_manifest.is_valid()) {
    report.blocking_reasons.push_back(make_validation_error(
        "verified manifest must stay structurally valid before ota.compatibility",
        "ota.compatibility"));
    return report;
  }

  if (!capability.is_valid()) {
    report.blocking_reasons.push_back(make_validation_error(
        "device capability snapshot must declare supported_hardware and unique dependency refs",
        "ota.compatibility"));
    return report;
  }

  if (!profile.is_valid()) {
    report.blocking_reasons.push_back(make_validation_error(
        "artifact compatibility profile must declare profile_name and unique forbidden dependencies",
        "ota.compatibility"));
    return report;
  }

  if (!contains(verified_manifest.compatible_profiles, profile.profile_name)) {
    report.blocking_reasons.push_back(make_error_info(
        "verified manifest does not allow the active profile",
        "ota.compatibility.profile"));
  }

  std::vector<std::string> available_dependency_refs =
      capability.available_dependency_refs;
  for (const auto& artifact : verified_manifest.artifact_list) {
    available_dependency_refs.push_back(artifact.artifact_id);
  }

  for (const auto& artifact : verified_manifest.artifact_list) {
    if (!intersects(artifact.hardware_selector, capability.supported_hardware)) {
      report.blocking_reasons.push_back(make_error_info(
          "artifact hardware_selector does not match device capabilities",
          "ota.compatibility.hardware"));
      continue;
    }

    if (artifact.artifact_class == ArtifactClass::SlotBound &&
        !profile.slot_bound_allowed) {
      report.blocking_reasons.push_back(make_error_info(
          "slot_bound artifacts are disabled for the active profile",
          "ota.compatibility.profile"));
      continue;
    }

    if (artifact.artifact_class == ArtifactClass::RepoBound &&
        !profile.repo_bound_allowed) {
      report.blocking_reasons.push_back(make_error_info(
          "repo_bound artifacts are disabled for the active profile",
          "ota.compatibility.profile"));
      continue;
    }

    const auto dependency_conflict = std::find_if(
        artifact.dependency_refs.begin(),
        artifact.dependency_refs.end(),
        [&](const std::string& dependency_ref) {
          return contains(profile.forbidden_dependency_refs, dependency_ref) ||
                 !contains(available_dependency_refs, dependency_ref);
        });
    if (dependency_conflict != artifact.dependency_refs.end()) {
      report.blocking_reasons.push_back(make_error_info(
          "artifact dependency_refs contain a forbidden or unavailable dependency",
          "ota.compatibility.dependency"));
      continue;
    }

    report.accepted_artifact_ids.push_back(artifact.artifact_id);
  }

  if (!report.blocking_reasons.empty()) {
    report.accepted_artifact_ids.clear();
    return report;
  }

  report.compatible = true;
  return report;
}

}  // namespace dasall::infra::ota
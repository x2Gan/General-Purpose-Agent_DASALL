#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::ota {

enum class ArtifactClass {
  Unspecified = 0,
  SlotBound = 1,
  RepoBound = 2,
};

inline constexpr std::string_view artifact_class_name(ArtifactClass artifact_class) {
  switch (artifact_class) {
    case ArtifactClass::Unspecified:
      return "unspecified";
    case ArtifactClass::SlotBound:
      return "slot_bound";
    case ArtifactClass::RepoBound:
      return "repo_bound";
  }

  return "unspecified";
}

[[nodiscard]] inline bool has_unique_non_empty_values(
    const std::vector<std::string>& values) {
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (values[index].empty()) {
      return false;
    }

    if (std::find(values.begin() + static_cast<std::ptrdiff_t>(index + 1),
                  values.end(),
                  values[index]) != values.end()) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] inline bool is_contract_error_info_populated(
    const contracts::ErrorInfo& error_info) {
  return error_info.failure_type.has_value() &&
         *error_info.failure_type != contracts::ResultCodeCategory::Unknown &&
         error_info.retryable.has_value() && error_info.safe_to_replan.has_value() &&
         error_info.details.code.has_value() && !error_info.details.message.empty() &&
         !error_info.details.stage.empty() && !error_info.source_ref.ref_type.empty() &&
         !error_info.source_ref.ref_id.empty();
}

struct UpgradeRequester {
  std::string actor_ref;
  std::string request_id;

  [[nodiscard]] bool is_valid() const {
    return !actor_ref.empty() && !request_id.empty();
  }
};

struct UpgradePlan {
  std::string plan_id;
  UpgradeRequester requested_by;
  std::string target_scope;
  std::vector<std::string> artifact_refs;
  std::string strategy;
  bool validate_only = false;

  [[nodiscard]] bool is_valid() const {
    return !plan_id.empty() && requested_by.is_valid() && !target_scope.empty() &&
           !artifact_refs.empty() && has_unique_non_empty_values(artifact_refs) &&
           !strategy.empty();
  }
};

struct PackageDescriptor {
  std::string package_id;
  std::string package_uri;
  std::string manifest_version;
  std::string package_kind;
  std::string signed_metadata_ref;
  std::uint64_t size_bytes = 0;

  [[nodiscard]] bool is_valid() const {
    return !package_id.empty() && !package_uri.empty() && !manifest_version.empty() &&
           !package_kind.empty() && !signed_metadata_ref.empty() && size_bytes > 0;
  }
};

struct ArtifactDescriptor {
  std::string artifact_id;
  ArtifactClass artifact_class = ArtifactClass::Unspecified;
  std::string target_slot_group;
  std::string version;
  std::vector<std::string> hardware_selector;
  std::vector<std::string> dependency_refs;

  [[nodiscard]] bool uses_frozen_artifact_class() const {
    return artifact_class == ArtifactClass::SlotBound ||
           artifact_class == ArtifactClass::RepoBound;
  }

  [[nodiscard]] bool is_valid() const {
    return !artifact_id.empty() && uses_frozen_artifact_class() &&
           !target_slot_group.empty() && !version.empty() &&
           !hardware_selector.empty() && has_unique_non_empty_values(hardware_selector) &&
           has_unique_non_empty_values(dependency_refs);
  }
};

struct VerifiedPackageManifest {
  std::string package_id;
  bool signature_ok = false;
  std::vector<std::string> hash_set;
  std::uint64_t release_counter = 0;
  std::vector<std::string> compatible_profiles;
  std::vector<ArtifactDescriptor> artifact_list;

  [[nodiscard]] bool is_valid() const {
    return !package_id.empty() && signature_ok && !hash_set.empty() &&
           has_unique_non_empty_values(hash_set) && release_counter > 0 &&
           !compatible_profiles.empty() &&
           has_unique_non_empty_values(compatible_profiles) && !artifact_list.empty();
  }
};

struct PrecheckReport {
  bool health_ok = false;
  bool resource_ok = false;
  bool compatibility_ok = false;
  bool policy_ok = false;
  std::vector<contracts::ErrorInfo> blocking_reasons;

  [[nodiscard]] bool passed() const {
    return health_ok && resource_ok && compatibility_ok && policy_ok;
  }

  [[nodiscard]] bool uses_contract_error_types_only() const {
    return std::all_of(blocking_reasons.begin(),
                       blocking_reasons.end(),
                       [](const contracts::ErrorInfo& error_info) {
                         return is_contract_error_info_populated(error_info);
                       });
  }

  [[nodiscard]] bool is_valid() const {
    if (passed()) {
      return blocking_reasons.empty();
    }

    return !blocking_reasons.empty() && uses_contract_error_types_only();
  }
};

struct SlotPlan {
  std::string active_slot;
  std::string target_slot;
  std::string slot_group;
  std::string switch_policy;
  std::string confirm_deadline;

  [[nodiscard]] bool targets_inactive_slot() const {
    return !active_slot.empty() && !target_slot.empty() && active_slot != target_slot;
  }

  [[nodiscard]] bool is_valid() const {
    return targets_inactive_slot() && !slot_group.empty() && !switch_policy.empty() &&
           !confirm_deadline.empty();
  }
};

struct RollbackToken {
  std::string rollback_id;
  std::string previous_boot_target;
  std::vector<std::string> staged_artifacts;
  std::string created_at;
  std::string expires_at;

  [[nodiscard]] bool is_valid() const {
    return !rollback_id.empty() && !previous_boot_target.empty() &&
           !staged_artifacts.empty() && has_unique_non_empty_values(staged_artifacts) &&
           !created_at.empty() && !expires_at.empty();
  }
};

struct InstallEvidence {
  std::string artifact_id;
  std::string written_target;
  std::string checksum;
  std::string install_ts;
  std::string installer_version;

  [[nodiscard]] bool is_valid() const {
    return !artifact_id.empty() && !written_target.empty() && !checksum.empty() &&
           !install_ts.empty() && !installer_version.empty();
  }
};

struct UpgradeOutcome {
  std::string phase;
  std::optional<contracts::ResultCode> result_code;
  bool rollback_applied = false;
  std::vector<std::string> final_version_set;
  std::string evidence_ref;

  [[nodiscard]] bool is_success_phase() const {
    return phase == "validated" || phase == "success" ||
           phase == "rollback_applied";
  }

  [[nodiscard]] bool is_failure_phase() const {
    return phase == "precheck_failed" || phase == "verify_failed" ||
           phase == "install_failed" || phase == "confirm_timeout" ||
           phase == "rollback_failed";
  }

  [[nodiscard]] bool references_only_contract_result_code() const {
    return !result_code.has_value() ||
           contracts::classify_result_code(*result_code) !=
               contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool is_success() const {
    const bool allows_empty_version_set = phase == "validated";
    return is_success_phase() && !result_code.has_value() && !evidence_ref.empty() &&
           (allows_empty_version_set ||
            (!final_version_set.empty() && has_unique_non_empty_values(final_version_set)));
  }

  [[nodiscard]] bool is_failure() const {
    return is_failure_phase() && result_code.has_value() &&
           references_only_contract_result_code() && !evidence_ref.empty();
  }

  [[nodiscard]] bool is_valid() const {
    return (is_success() || is_failure()) && references_only_contract_result_code();
  }
};

struct OTAStatusSnapshot {
  std::string last_plan_id;
  std::string state;
  std::string active_slot;
  bool pending_confirm = false;
  std::optional<contracts::ResultCode> last_failure_code;
  std::uint32_t backlog_count = 0;

  [[nodiscard]] bool references_only_contract_result_code() const {
    return !last_failure_code.has_value() ||
           contracts::classify_result_code(*last_failure_code) !=
               contracts::ResultCodeCategory::Unknown;
  }

  [[nodiscard]] bool is_valid() const {
    return !last_plan_id.empty() && !state.empty() && !active_slot.empty() &&
           references_only_contract_result_code();
  }
};

}  // namespace dasall::infra::ota
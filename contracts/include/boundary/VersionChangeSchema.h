#pragma once

#include <string_view>

namespace dasall::contracts {

// VersionBumpKind captures the intended semantic-version bump class for a
// contract change record.
enum class VersionBumpKind {
  Patch,
  Minor,
  Major,
};

// CompatibilityLevel captures compatibility expectations for downstream
// consumers of the changed contract.
enum class CompatibilityLevel {
  BackwardCompatible,
  PartiallyCompatible,
  Breaking,
};

// VersionChangeSchema is the normalized template payload for one contracts
// version-change review record.
struct VersionChangeSchema {
  std::string_view change_id;
  std::string_view contract_domain;
  std::string_view previous_version;
  std::string_view target_version;
  VersionBumpKind bump_kind = VersionBumpKind::Patch;
  CompatibilityLevel compatibility = CompatibilityLevel::BackwardCompatible;
  std::string_view summary;
  std::string_view rationale;
  bool migration_required = false;
  bool breaking_review_required = false;
  std::string_view migration_plan;
  std::string_view deprecation_window;
};

// Returns a stable bump-kind label for diagnostics and contract tests.
constexpr std::string_view version_bump_kind_name(VersionBumpKind bump_kind) {
  switch (bump_kind) {
    case VersionBumpKind::Patch:
      return "patch";
    case VersionBumpKind::Minor:
      return "minor";
    case VersionBumpKind::Major:
      return "major";
  }

  return "unknown";
}

// Returns a stable compatibility label for diagnostics and contract tests.
constexpr std::string_view compatibility_level_name(
    CompatibilityLevel compatibility) {
  switch (compatibility) {
    case CompatibilityLevel::BackwardCompatible:
      return "backward_compatible";
    case CompatibilityLevel::PartiallyCompatible:
      return "partially_compatible";
    case CompatibilityLevel::Breaking:
      return "breaking";
  }

  return "unknown";
}

}  // namespace dasall::contracts
#pragma once

#include <string_view>

#include "boundary/VersionChangeSchema.h"

namespace dasall::contracts {

// VersionChangeValidationResult provides structured output for gate decisions
// and test assertions.
struct VersionChangeValidationResult {
  bool ok = false;
  bool required_fields_ok = false;
  bool semver_format_ok = false;
  bool breaking_rules_ok = false;
  bool non_breaking_rules_ok = false;
  bool breaking = false;
  std::string_view first_failed_check = "not-run";
  std::string_view reason = "version change validation not yet run";
};

// Returns true if a text field is non-empty.
constexpr bool has_text(std::string_view value) { return !value.empty(); }

// Returns true if the input follows the minimal SemVer syntax X.Y.Z where each
// component contains at least one digit.
constexpr bool is_valid_semver_triplet(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  int dot_count = 0;
  int digits_in_segment = 0;

  for (char ch : value) {
    if (ch >= '0' && ch <= '9') {
      ++digits_in_segment;
      continue;
    }

    if (ch == '.') {
      if (digits_in_segment == 0) {
        return false;
      }

      ++dot_count;
      digits_in_segment = 0;
      continue;
    }

    return false;
  }

  return dot_count == 2 && digits_in_segment > 0;
}

// Returns true if this record must be treated as breaking by policy.
constexpr bool version_change_is_breaking(const VersionChangeSchema& schema) {
  return schema.bump_kind == VersionBumpKind::Major ||
         schema.compatibility == CompatibilityLevel::Breaking;
}

// Validates fields that are always required regardless of breaking level.
constexpr bool has_required_version_change_fields(
    const VersionChangeSchema& schema) {
  return has_text(schema.change_id) && has_text(schema.contract_domain) &&
         has_text(schema.previous_version) && has_text(schema.target_version) &&
         has_text(schema.summary) && has_text(schema.rationale);
}

// Validates semantic-version formatting constraints for both versions.
constexpr bool has_valid_version_change_semver(
    const VersionChangeSchema& schema) {
  return is_valid_semver_triplet(schema.previous_version) &&
         is_valid_semver_triplet(schema.target_version);
}

// Validates strict requirements for breaking changes.
constexpr bool has_valid_breaking_requirements(
    const VersionChangeSchema& schema) {
  return schema.breaking_review_required && schema.migration_required &&
         has_text(schema.migration_plan) && has_text(schema.deprecation_window);
}

// Validates non-breaking constraints. Non-breaking changes must not require a
// dedicated breaking-review gate.
constexpr bool has_valid_non_breaking_requirements(
    const VersionChangeSchema& schema) {
  return !schema.breaking_review_required;
}

// Main validator for the version-change schema.
inline constexpr VersionChangeValidationResult validate_version_change_schema(
    const VersionChangeSchema& schema) {
  if (!has_required_version_change_fields(schema)) {
    return VersionChangeValidationResult{
        .ok = false,
        .required_fields_ok = false,
        .semver_format_ok = false,
        .breaking_rules_ok = false,
        .non_breaking_rules_ok = false,
        .breaking = false,
        .first_failed_check = "required-fields",
        .reason = "version change schema is missing required fields",
    };
  }

  if (!has_valid_version_change_semver(schema)) {
    return VersionChangeValidationResult{
        .ok = false,
        .required_fields_ok = true,
        .semver_format_ok = false,
        .breaking_rules_ok = false,
        .non_breaking_rules_ok = false,
        .breaking = false,
        .first_failed_check = "semver-format",
        .reason = "version fields must follow X.Y.Z semver format",
    };
  }

  const bool breaking = version_change_is_breaking(schema);
  if (breaking) {
    if (!has_valid_breaking_requirements(schema)) {
      return VersionChangeValidationResult{
          .ok = false,
          .required_fields_ok = true,
          .semver_format_ok = true,
          .breaking_rules_ok = false,
          .non_breaking_rules_ok = true,
          .breaking = true,
          .first_failed_check = "breaking-requirements",
          .reason =
              "breaking change must include review gate, migration plan, and deprecation window",
      };
    }

    return VersionChangeValidationResult{
        .ok = true,
        .required_fields_ok = true,
        .semver_format_ok = true,
        .breaking_rules_ok = true,
        .non_breaking_rules_ok = true,
        .breaking = true,
        .first_failed_check = "none",
        .reason = "breaking version change schema is valid",
    };
  }

  if (!has_valid_non_breaking_requirements(schema)) {
    return VersionChangeValidationResult{
        .ok = false,
        .required_fields_ok = true,
        .semver_format_ok = true,
        .breaking_rules_ok = true,
        .non_breaking_rules_ok = false,
        .breaking = false,
        .first_failed_check = "non-breaking-requirements",
        .reason =
            "non-breaking change must not enable breaking-only review requirements",
    };
  }

  return VersionChangeValidationResult{
      .ok = true,
      .required_fields_ok = true,
      .semver_format_ok = true,
      .breaking_rules_ok = true,
      .non_breaking_rules_ok = true,
      .breaking = false,
      .first_failed_check = "none",
      .reason = "non-breaking version change schema is valid",
  };
}

// Convenience predicate for callers that only require binary pass/fail output.
inline constexpr bool can_accept_version_change(
    const VersionChangeSchema& schema) {
  return validate_version_change_schema(schema).ok;
}

}  // namespace dasall::contracts
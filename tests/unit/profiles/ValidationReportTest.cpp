#include <array>
#include <exception>
#include <iostream>
#include <set>

#include "ValidationReport.h"
#include "support/TestAssertions.h"

namespace {

void test_profile_error_codes_are_unique_and_classified() {
  using dasall::profiles::ProfileErrorCategory;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::classify_profile_error_code;
  using dasall::profiles::profile_error_code_name;
  using dasall::tests::support::assert_true;

  const std::array codes = {
      ProfileErrorCode::ProfileNotFound,
      ProfileErrorCode::CatalogUnavailable,
      ProfileErrorCode::SchemaInvalid,
      ProfileErrorCode::PlatformMismatch,
      ProfileErrorCode::ModuleIncompatible,
      ProfileErrorCode::RequiredAdapterMissing,
      ProfileErrorCode::OverrideInvalid,
      ProfileErrorCode::LastKnownGoodUnavailable,
  };

  std::set<int> raw_codes;
  std::set<std::string_view> raw_names;
  for (const auto code : codes) {
    raw_codes.insert(static_cast<int>(code));
    raw_names.insert(profile_error_code_name(code));
  }

  assert_true(raw_codes.size() == codes.size(),
              "profile error codes should keep unique numeric values");
  assert_true(raw_names.size() == codes.size(),
              "profile error codes should keep unique stable names");
  assert_true(classify_profile_error_code(ProfileErrorCode::CatalogUnavailable) ==
                  ProfileErrorCategory::Catalog,
              "catalog unavailable should stay in catalog category");
  assert_true(classify_profile_error_code(ProfileErrorCode::SchemaInvalid) ==
                  ProfileErrorCategory::Schema,
              "schema invalid should stay in schema category");
  assert_true(classify_profile_error_code(ProfileErrorCode::OverrideInvalid) ==
                  ProfileErrorCategory::Override,
              "override invalid should stay in override category");
  assert_true(classify_profile_error_code(ProfileErrorCode::LastKnownGoodUnavailable) ==
                  ProfileErrorCategory::Recovery,
              "last known good unavailable should stay in recovery category");
}

void test_validation_report_distinguishes_blocking_and_warning_states() {
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ValidationReport;
  using dasall::tests::support::assert_true;

  const ValidationReport compatible_report{};
  const ValidationReport warning_report{
      .blocking_errors = {},
      .warnings = {ProfileErrorCode::CatalogUnavailable},
      .dependency_gaps = {"infra.metrics"},
      .compatibility_state = ProfileCompatibilityState::Warning,
  };
  const ValidationReport blocking_report{
      .blocking_errors = {ProfileErrorCode::SchemaInvalid},
      .warnings = {ProfileErrorCode::CatalogUnavailable},
      .dependency_gaps = {"infra.config"},
      .compatibility_state = ProfileCompatibilityState::Blocked,
  };

  assert_true(compatible_report.has_consistent_values(),
              "empty validation report should stay compatible");
  assert_true(compatible_report.can_activate(),
              "compatible validation report should allow activation");
  assert_true(warning_report.has_consistent_values(),
              "warning-only validation report should stay consistent");
  assert_true(warning_report.can_activate(),
              "warning-only validation report should still allow activation");
  assert_true(blocking_report.has_consistent_values(),
              "blocking validation report should stay consistent when state is blocked");
  assert_true(!blocking_report.can_activate(),
              "blocking validation report should reject activation");
}

void test_validation_report_rejects_duplicate_or_overlapping_codes() {
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ValidationReport;
  using dasall::tests::support::assert_true;

  const ValidationReport duplicate_blocking{
      .blocking_errors = {ProfileErrorCode::SchemaInvalid, ProfileErrorCode::SchemaInvalid},
      .warnings = {},
      .dependency_gaps = {},
      .compatibility_state = ProfileCompatibilityState::Blocked,
  };
  const ValidationReport overlapping_codes{
      .blocking_errors = {ProfileErrorCode::OverrideInvalid},
      .warnings = {ProfileErrorCode::OverrideInvalid},
      .dependency_gaps = {},
      .compatibility_state = ProfileCompatibilityState::Blocked,
  };
  const ValidationReport inconsistent_state{
      .blocking_errors = {ProfileErrorCode::ModuleIncompatible},
      .warnings = {},
      .dependency_gaps = {},
      .compatibility_state = ProfileCompatibilityState::Warning,
  };

  assert_true(!duplicate_blocking.has_consistent_values(),
              "validation report should reject duplicate blocking error codes");
  assert_true(!overlapping_codes.has_consistent_values(),
              "validation report should reject overlapping blocking and warning codes");
  assert_true(!inconsistent_state.has_consistent_values(),
              "validation report should reject non-blocked state when blocking errors exist");
}

}  // namespace

int main() {
  try {
    test_profile_error_codes_are_unique_and_classified();
    test_validation_report_distinguishes_blocking_and_warning_states();
    test_validation_report_rejects_duplicate_or_overlapping_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
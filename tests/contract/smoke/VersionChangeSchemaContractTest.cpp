#include <exception>
#include <iostream>
#include <string>

#include "boundary/VersionChangeGuards.h"
#include "boundary/VersionChangeSchema.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::CompatibilityLevel;
using dasall::contracts::VersionBumpKind;
using dasall::contracts::VersionChangeSchema;
using dasall::contracts::can_accept_version_change;
using dasall::contracts::compatibility_level_name;
using dasall::contracts::has_valid_version_change_semver;
using dasall::contracts::is_valid_semver_triplet;
using dasall::contracts::validate_version_change_schema;
using dasall::contracts::version_bump_kind_name;
using dasall::contracts::version_change_is_breaking;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds a baseline non-breaking template instance used by most tests.
VersionChangeSchema make_non_breaking_template() {
  return VersionChangeSchema{
      .change_id = "wp05-t018-nb-001",
      .contract_domain = "boundary",
      .previous_version = "1.2.0",
      .target_version = "1.2.1",
      .bump_kind = VersionBumpKind::Patch,
      .compatibility = CompatibilityLevel::BackwardCompatible,
      .summary = "add optional audit tag in metadata",
      .rationale = "improve traceability without changing required fields",
      .migration_required = false,
      .breaking_review_required = false,
      .migration_plan = "",
      .deprecation_window = "",
  };
}

// Builds a baseline breaking template instance that includes all mandatory
// migration and review fields.
VersionChangeSchema make_breaking_template() {
  return VersionChangeSchema{
      .change_id = "wp05-t018-br-001",
      .contract_domain = "event",
      .previous_version = "2.0.0",
      .target_version = "3.0.0",
      .bump_kind = VersionBumpKind::Major,
      .compatibility = CompatibilityLevel::Breaking,
      .summary = "rename core event header key",
      .rationale = "align event envelope key with frozen naming policy",
      .migration_required = true,
      .breaking_review_required = true,
      .migration_plan = "ship adapter layer for one release cycle",
      .deprecation_window = "2 minor releases",
  };
}

// Positive coverage: non-breaking record with valid semver and required fields
// should pass validation.
void test_non_breaking_template_passes() {
  const auto schema = make_non_breaking_template();
  const auto result = validate_version_change_schema(schema);

  assert_true(result.ok,
              "non-breaking template should pass validation");
  assert_true(!result.breaking,
              "non-breaking template should not be marked as breaking");
  assert_true(can_accept_version_change(schema),
              "can_accept helper should match successful validation");
}

// Positive coverage: breaking record with review and migration details should
// pass validation.
void test_breaking_template_passes_with_required_fields() {
  const auto schema = make_breaking_template();
  const auto result = validate_version_change_schema(schema);

  assert_true(result.ok,
              "breaking template should pass when all required fields exist");
  assert_true(result.breaking,
              "breaking template should be marked as breaking");
  assert_true(version_change_is_breaking(schema),
              "breaking predicate should detect major+breaking changes");
}

// Negative coverage: invalid semver format should fail before compatibility
// checks.
void test_invalid_semver_is_rejected() {
  auto schema = make_non_breaking_template();
  schema.previous_version = "1.2";

  const auto result = validate_version_change_schema(schema);

  assert_true(!result.ok,
              "invalid semver should fail validation");
  assert_equal(std::string("semver-format"),
               std::string(result.first_failed_check),
               "first failed check should report semver-format");
  assert_true(!has_valid_version_change_semver(schema),
              "semver helper should report false for malformed version");
}

// Negative coverage: missing required text field should fail required-field
// validation.
void test_missing_required_field_is_rejected() {
  auto schema = make_non_breaking_template();
  schema.summary = "";

  const auto result = validate_version_change_schema(schema);

  assert_true(!result.ok,
              "missing summary should fail validation");
  assert_equal(std::string("required-fields"),
               std::string(result.first_failed_check),
               "first failed check should report required-fields");
}

// Negative coverage: breaking changes must provide breaking review and
// migration details.
void test_breaking_change_without_review_and_migration_is_rejected() {
  auto schema = make_breaking_template();
  schema.breaking_review_required = false;
  schema.migration_required = false;
  schema.migration_plan = "";
  schema.deprecation_window = "";

  const auto result = validate_version_change_schema(schema);

  assert_true(!result.ok,
              "breaking change without required migration fields must fail");
  assert_equal(std::string("breaking-requirements"),
               std::string(result.first_failed_check),
               "first failed check should report breaking-requirements");
}

// Positive coverage: enum labels must remain stable for diagnostics and
// downstream reporting.
void test_enum_name_labels_are_stable() {
  assert_equal(std::string("major"),
               std::string(version_bump_kind_name(VersionBumpKind::Major)),
               "major bump label must stay stable");
  assert_equal(
      std::string("breaking"),
      std::string(compatibility_level_name(CompatibilityLevel::Breaking)),
      "breaking compatibility label must stay stable");
  assert_true(is_valid_semver_triplet("10.20.30"),
              "three numeric semver segments should be accepted");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Common runner keeps output aligned with existing contract smoke tests.
  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  // Banner keeps ctest output traceable to WP05-T018-B.
  std::cout << "VersionChangeSchemaContractTest - WP05-T018-B\n";

  run_test("test_non_breaking_template_passes",
           test_non_breaking_template_passes);
  run_test("test_breaking_template_passes_with_required_fields",
           test_breaking_template_passes_with_required_fields);
  run_test("test_invalid_semver_is_rejected",
           test_invalid_semver_is_rejected);
  run_test("test_missing_required_field_is_rejected",
           test_missing_required_field_is_rejected);
  run_test("test_breaking_change_without_review_and_migration_is_rejected",
           test_breaking_change_without_review_and_migration_is_rejected);
  run_test("test_enum_name_labels_are_stable",
           test_enum_name_labels_are_stable);

  // Summary output follows existing contract test conventions.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
#include <exception>
#include <iostream>
#include <string>

#include "boundary/BreakingReviewGuards.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::BreakingReviewContext;
using dasall::contracts::BreakingReviewDecision;
using dasall::contracts::CompatibilityLevel;
using dasall::contracts::VersionBumpKind;
using dasall::contracts::VersionChangeSchema;
using dasall::contracts::breaking_review_decision_name;
using dasall::contracts::can_pass_breaking_review;
using dasall::contracts::evaluate_breaking_review_gate;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Builds a non-breaking context that should bypass dedicated breaking review.
BreakingReviewContext make_non_breaking_context() {
  return BreakingReviewContext{
      .schema = VersionChangeSchema{
          .change_id = "wp05-t019-nb-001",
          .contract_domain = "boundary",
          .previous_version = "1.3.0",
          .target_version = "1.3.1",
          .bump_kind = VersionBumpKind::Patch,
          .compatibility = CompatibilityLevel::BackwardCompatible,
          .summary = "add optional metadata field",
          .rationale = "non-breaking extension",
          .migration_required = false,
          .breaking_review_required = false,
          .migration_plan = "",
          .deprecation_window = "",
      },
      .owner_approved = false,
      .consumer_approved = false,
      .migration_validation_evidence = "",
      .review_record_link = "",
  };
}

// Builds a fully approved breaking-review context.
BreakingReviewContext make_approved_breaking_context() {
  return BreakingReviewContext{
      .schema = VersionChangeSchema{
          .change_id = "wp05-t019-br-001",
          .contract_domain = "event",
          .previous_version = "2.2.0",
          .target_version = "3.0.0",
          .bump_kind = VersionBumpKind::Major,
          .compatibility = CompatibilityLevel::Breaking,
          .summary = "rename required event header",
          .rationale = "align with frozen naming boundary",
          .migration_required = true,
          .breaking_review_required = true,
          .migration_plan = "ship compatibility adapter for one release",
          .deprecation_window = "2 minor releases",
      },
      .owner_approved = true,
      .consumer_approved = true,
      .migration_validation_evidence = "tests/contract/event/EventEnvelopeCompatibilityContractTest.cpp",
      .review_record_link = "docs/todos/contracts-freeze/deliverables/WP05-T019-变更流程清单.md",
  };
}

// Positive coverage: non-breaking changes should pass as NotRequired.
void test_non_breaking_change_does_not_require_special_review() {
  const auto result = evaluate_breaking_review_gate(make_non_breaking_context());

  assert_true(result.allowed,
              "non-breaking change should pass without dedicated breaking review");
  assert_equal(std::string("not_required"),
               std::string(breaking_review_decision_name(result.decision)),
               "non-breaking decision should be not_required");
}

// Positive coverage: fully satisfied breaking context should be approved.
void test_breaking_change_with_complete_reviews_is_approved() {
  const auto context = make_approved_breaking_context();
  const auto result = evaluate_breaking_review_gate(context);

  assert_true(result.allowed,
              "breaking change with full evidence should be approved");
  assert_equal(std::string("approved"),
               std::string(breaking_review_decision_name(result.decision)),
               "approved breaking review should report approved decision");
  assert_true(can_pass_breaking_review(context),
              "binary helper should agree with approved gate result");
}

// Negative coverage: missing owner approval must reject breaking change.
void test_breaking_change_without_owner_approval_is_rejected() {
  auto context = make_approved_breaking_context();
  context.owner_approved = false;

  const auto result = evaluate_breaking_review_gate(context);

  assert_true(!result.allowed,
              "breaking change without owner approval must be rejected");
  assert_equal(std::string("owner-approval"),
               std::string(result.first_failed_check),
               "first failed check should report owner approval");
}

// Negative coverage: missing consumer approval must reject breaking change.
void test_breaking_change_without_consumer_approval_is_rejected() {
  auto context = make_approved_breaking_context();
  context.consumer_approved = false;

  const auto result = evaluate_breaking_review_gate(context);

  assert_true(!result.allowed,
              "breaking change without consumer approval must be rejected");
  assert_equal(std::string("consumer-approval"),
               std::string(result.first_failed_check),
               "first failed check should report consumer approval");
}

// Negative coverage: missing migration evidence must reject breaking change.
void test_breaking_change_without_migration_evidence_is_rejected() {
  auto context = make_approved_breaking_context();
  context.migration_validation_evidence = "";

  const auto result = evaluate_breaking_review_gate(context);

  assert_true(!result.allowed,
              "breaking change without migration evidence must be rejected");
  assert_equal(std::string("migration-evidence"),
               std::string(result.first_failed_check),
               "first failed check should report migration evidence");
}

// Negative coverage: missing review record must reject breaking change.
void test_breaking_change_without_review_record_is_rejected() {
  auto context = make_approved_breaking_context();
  context.review_record_link = "";

  const auto result = evaluate_breaking_review_gate(context);

  assert_true(!result.allowed,
              "breaking change without review record must be rejected");
  assert_equal(std::string("review-record"),
               std::string(result.first_failed_check),
               "first failed check should report review record");
}

// Negative coverage: invalid schema must fail at schema gate before approval
// checks.
void test_invalid_schema_is_rejected_before_review_checks() {
  auto context = make_approved_breaking_context();
  context.schema.target_version = "3.0";

  const auto result = evaluate_breaking_review_gate(context);

  assert_true(!result.allowed,
              "invalid schema must reject breaking review gate");
  assert_equal(std::string("version-change-schema"),
               std::string(result.first_failed_check),
               "first failed check should report schema validation");
}

}  // namespace

int main() {
  int passed = 0;
  int failed = 0;

  // Common runner keeps output style aligned with other contract smoke tests.
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

  // Banner keeps ctest output traceable to WP05-T019-B.
  std::cout << "BreakingReviewContractTest - WP05-T019-B\n";

  run_test("test_non_breaking_change_does_not_require_special_review",
           test_non_breaking_change_does_not_require_special_review);
  run_test("test_breaking_change_with_complete_reviews_is_approved",
           test_breaking_change_with_complete_reviews_is_approved);
  run_test("test_breaking_change_without_owner_approval_is_rejected",
           test_breaking_change_without_owner_approval_is_rejected);
  run_test("test_breaking_change_without_consumer_approval_is_rejected",
           test_breaking_change_without_consumer_approval_is_rejected);
  run_test("test_breaking_change_without_migration_evidence_is_rejected",
           test_breaking_change_without_migration_evidence_is_rejected);
  run_test("test_breaking_change_without_review_record_is_rejected",
           test_breaking_change_without_review_record_is_rejected);
  run_test("test_invalid_schema_is_rejected_before_review_checks",
           test_invalid_schema_is_rejected_before_review_checks);

  // Summary output follows the repository contract-test convention.
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
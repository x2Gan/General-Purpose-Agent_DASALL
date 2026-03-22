#pragma once

#include <string_view>

#include "boundary/VersionChangeGuards.h"

namespace dasall::contracts {

// BreakingReviewDecision encodes the three outcomes needed by WP05-T019:
// non-breaking direct pass, breaking approved, and breaking rejected.
enum class BreakingReviewDecision {
  NotRequired,
  Approved,
  Rejected,
};

// BreakingReviewContext contains review-evidence inputs required by the
// breaking-review gate on top of the version-change schema itself.
struct BreakingReviewContext {
  VersionChangeSchema schema;
  bool owner_approved = false;
  bool consumer_approved = false;
  std::string_view migration_validation_evidence;
  std::string_view review_record_link;
};

// BreakingReviewGateResult provides structured diagnostics for CI and contract
// tests, including the first failing check.
struct BreakingReviewGateResult {
  bool allowed = false;
  BreakingReviewDecision decision = BreakingReviewDecision::Rejected;
  bool schema_valid = false;
  bool breaking = false;
  bool owner_approved = false;
  bool consumer_approved = false;
  bool migration_evidence_ready = false;
  bool review_record_ready = false;
  std::string_view first_failed_check = "not-run";
  std::string_view reason = "breaking review gate not yet evaluated";
};

// Stable string label for review-decision diagnostics.
constexpr std::string_view breaking_review_decision_name(
    BreakingReviewDecision decision) {
  switch (decision) {
    case BreakingReviewDecision::NotRequired:
      return "not_required";
    case BreakingReviewDecision::Approved:
      return "approved";
    case BreakingReviewDecision::Rejected:
      return "rejected";
  }

  return "unknown";
}

// True if migration evidence text is present.
constexpr bool has_migration_review_evidence(
    const BreakingReviewContext& context) {
  return !context.migration_validation_evidence.empty();
}

// True if a review record link is present.
constexpr bool has_breaking_review_record(
    const BreakingReviewContext& context) {
  return !context.review_record_link.empty();
}

// Main gate evaluator for WP05-T019 breaking-review policy.
inline constexpr BreakingReviewGateResult evaluate_breaking_review_gate(
    const BreakingReviewContext& context) {
  const auto schema_result = validate_version_change_schema(context.schema);
  if (!schema_result.ok) {
    return BreakingReviewGateResult{
        .allowed = false,
        .decision = BreakingReviewDecision::Rejected,
        .schema_valid = false,
        .breaking = false,
        .owner_approved = context.owner_approved,
        .consumer_approved = context.consumer_approved,
        .migration_evidence_ready = has_migration_review_evidence(context),
        .review_record_ready = has_breaking_review_record(context),
        .first_failed_check = "version-change-schema",
        .reason = "breaking review gate rejected because version change schema is invalid",
    };
  }

  const bool breaking = version_change_is_breaking(context.schema);
  if (!breaking) {
    return BreakingReviewGateResult{
        .allowed = true,
        .decision = BreakingReviewDecision::NotRequired,
        .schema_valid = true,
        .breaking = false,
        .owner_approved = context.owner_approved,
        .consumer_approved = context.consumer_approved,
        .migration_evidence_ready = has_migration_review_evidence(context),
        .review_record_ready = has_breaking_review_record(context),
        .first_failed_check = "none",
        .reason = "breaking review not required for non-breaking change",
    };
  }

  if (!context.schema.breaking_review_required) {
    return BreakingReviewGateResult{
        .allowed = false,
        .decision = BreakingReviewDecision::Rejected,
        .schema_valid = true,
        .breaking = true,
        .owner_approved = context.owner_approved,
        .consumer_approved = context.consumer_approved,
        .migration_evidence_ready = has_migration_review_evidence(context),
        .review_record_ready = has_breaking_review_record(context),
        .first_failed_check = "breaking-review-required",
        .reason = "breaking change must explicitly require a dedicated breaking review",
    };
  }

  if (!context.owner_approved) {
    return BreakingReviewGateResult{
        .allowed = false,
        .decision = BreakingReviewDecision::Rejected,
        .schema_valid = true,
        .breaking = true,
        .owner_approved = false,
        .consumer_approved = context.consumer_approved,
        .migration_evidence_ready = has_migration_review_evidence(context),
        .review_record_ready = has_breaking_review_record(context),
        .first_failed_check = "owner-approval",
        .reason = "breaking change must be approved by contract owner",
    };
  }

  if (!context.consumer_approved) {
    return BreakingReviewGateResult{
        .allowed = false,
        .decision = BreakingReviewDecision::Rejected,
        .schema_valid = true,
        .breaking = true,
        .owner_approved = true,
        .consumer_approved = false,
        .migration_evidence_ready = has_migration_review_evidence(context),
        .review_record_ready = has_breaking_review_record(context),
        .first_failed_check = "consumer-approval",
        .reason = "breaking change must be approved by at least one primary consumer",
    };
  }

  if (!has_migration_review_evidence(context)) {
    return BreakingReviewGateResult{
        .allowed = false,
        .decision = BreakingReviewDecision::Rejected,
        .schema_valid = true,
        .breaking = true,
        .owner_approved = true,
        .consumer_approved = true,
        .migration_evidence_ready = false,
        .review_record_ready = has_breaking_review_record(context),
        .first_failed_check = "migration-evidence",
        .reason = "breaking change must provide migration validation evidence",
    };
  }

  if (!has_breaking_review_record(context)) {
    return BreakingReviewGateResult{
        .allowed = false,
        .decision = BreakingReviewDecision::Rejected,
        .schema_valid = true,
        .breaking = true,
        .owner_approved = true,
        .consumer_approved = true,
        .migration_evidence_ready = true,
        .review_record_ready = false,
        .first_failed_check = "review-record",
        .reason = "breaking change must provide a review record reference",
    };
  }

  return BreakingReviewGateResult{
      .allowed = true,
      .decision = BreakingReviewDecision::Approved,
      .schema_valid = true,
      .breaking = true,
      .owner_approved = true,
      .consumer_approved = true,
      .migration_evidence_ready = true,
      .review_record_ready = true,
      .first_failed_check = "none",
      .reason = "breaking review gate approved",
  };
}

// Binary convenience predicate for callers that only need allow/deny output.
inline constexpr bool can_pass_breaking_review(
    const BreakingReviewContext& context) {
  return evaluate_breaking_review_gate(context).allowed;
}

}  // namespace dasall::contracts
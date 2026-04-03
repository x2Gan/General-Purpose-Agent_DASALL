#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "audit/AuditExporter.h"
#include "audit/AuditExporterTypes.h"
#include "audit/AuditTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::AuditEvent make_event(std::string event_id,
                                     std::string actor,
                                     std::string action,
                                     std::string target,
                                     dasall::infra::AuditOutcome outcome,
                                     std::int64_t timestamp) {
  return dasall::infra::AuditEvent{
      .event_id = std::move(event_id),
      .action = std::move(action),
      .actor = std::move(actor),
      .target = std::move(target),
      .outcome = outcome,
      .evidence_ref = {.kind = dasall::infra::AuditEvidenceKind::ToolResult,
                       .ref = std::string("tool-ref")},
      .side_effects = {"effect-recorded"},
      .timestamp = timestamp,
  };
}

void test_export_query_freezes_time_window_and_filter_fields() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::ExportQuery;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ExportQuery{}.start_ts), std::int64_t>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.end_ts), std::int64_t>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.actor), std::string>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.action), std::string>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.target), std::string>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.outcome), AuditOutcome>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.page_token), std::string>);

  const ExportQuery query{
      .start_ts = 1711785600000,
      .end_ts = 1711789200000,
      .actor = std::string("ops-user"),
      .action = std::string("policy.patch"),
      .target = std::string("policy-bundle-v2"),
      .outcome = AuditOutcome::Succeeded,
      .page_token = std::string("cursor-002"),
  };

  assert_true(query.has_required_window(),
              "export query should require a non-empty start/end window once the object is frozen");
  assert_true(query.has_ordered_window(),
              "export query should keep the time window monotonic when start_ts and end_ts are both present");
  assert_true(query.requests_page_resume(),
              "export query should carry an explicit page token field for stable pagination resume");
  assert_true(query.filters_on_outcome(),
              "export query should allow the frozen outcome field to opt into explicit outcome filtering");
}

void test_export_query_rejects_missing_or_inverted_time_window() {
  using dasall::infra::ExportQuery;
  using dasall::tests::support::assert_true;

  const ExportQuery missing_window{
      .start_ts = 0,
      .end_ts = 1711789200000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };

  const ExportQuery inverted_window{
      .start_ts = 1711789200000,
      .end_ts = 1711785600000,
      .actor = std::string("ops-user"),
      .action = std::string("policy.patch"),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };

  assert_true(!missing_window.has_required_window(),
              "export query should reject missing start/end timestamps before any finer-grained filter semantics are added");
  assert_true(!inverted_window.has_ordered_window(),
              "export query should reject end_ts values that move backward before start_ts");
}

void test_export_result_freezes_records_checksum_and_resume_fields() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditOutcome;
  using dasall::infra::ExportResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ExportResult{}.records), std::vector<AuditEvent>>);
  static_assert(std::is_same_v<decltype(ExportResult{}.next_page_token), std::string>);
  static_assert(std::is_same_v<decltype(ExportResult{}.truncated), bool>);
  static_assert(std::is_same_v<decltype(ExportResult{}.checksum), std::string>);

  const ExportResult partial_page{
      .records = {AuditEvent{
          .event_id = std::string("audit-export-001"),
          .action = std::string("diagnostics.export"),
          .actor = std::string("ops-user"),
          .target = std::string("support-bundle"),
          .outcome = AuditOutcome::Succeeded,
          .evidence_ref = {.kind = AuditEvidenceKind::ToolResult,
                           .ref = std::string("tool-call-017")},
          .side_effects = {"bundle_written"},
          .timestamp = 1711785603000,
      }},
      .next_page_token = std::string("cursor-003"),
      .truncated = true,
      .checksum = std::string("sha256:abc123"),
  };

  const ExportResult final_page{
      .records = {},
      .next_page_token = std::string(),
      .truncated = false,
      .checksum = std::string("sha256:def456"),
  };

  assert_true(partial_page.has_checksum(),
              "export result should expose an explicit checksum once the output object is frozen");
  assert_true(partial_page.has_consistent_pagination(),
              "truncated export pages should carry a resume token so pagination remains stable");
  assert_true(final_page.is_complete_page(),
              "non-truncated export pages should represent an explicit final page without a resume token");
}

void test_export_result_rejects_inconsistent_truncation_state() {
  using dasall::infra::ExportResult;
  using dasall::tests::support::assert_true;

  const ExportResult missing_resume_token{
      .records = {},
      .next_page_token = std::string(),
      .truncated = true,
      .checksum = std::string("sha256:ghi789"),
  };

  const ExportResult unexpected_resume_token{
      .records = {},
      .next_page_token = std::string("cursor-004"),
      .truncated = false,
      .checksum = std::string("sha256:jkl012"),
  };

  assert_true(!missing_resume_token.has_consistent_pagination(),
              "truncated export results should not omit the next_page_token needed to resume paging");
  assert_true(!unexpected_resume_token.has_consistent_pagination(),
              "final export pages should not advertise a resume token when truncated is false");
}

void test_audit_exporter_applies_primary_and_extension_filters() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::ExportQuery;
  using dasall::infra::audit::AuditExporter;
  using dasall::tests::support::assert_true;

  const std::vector<dasall::infra::AuditEvent> primary_records{
      make_event("audit-export-001",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785601000),
      make_event("audit-export-002",
                 "runtime",
                 "diagnostics.export",
                 "policy-bundle",
                 AuditOutcome::Failed,
                 1711785602000),
      make_event("audit-export-003",
                 "runtime",
                 "tool.execute",
                 "shell",
                 AuditOutcome::Succeeded,
                 1711785603000),
  };
  const std::vector<dasall::infra::AuditEvent> fallback_records{
      make_event("audit-export-004",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785604000),
  };

  const AuditExporter exporter(&primary_records, &fallback_records);
  const ExportQuery query{
      .start_ts = 1711785600000,
      .end_ts = 1711785605000,
      .actor = std::string("runtime"),
      .action = std::string("diagnostics.export"),
      .target = std::string("support-bundle"),
      .outcome = AuditOutcome::Succeeded,
      .page_token = std::string(),
  };

  const auto result = exporter.export_records(query);
  assert_true(result.records.size() == 2,
              "AuditExporter should combine primary and fallback records, then keep only records that satisfy the frozen window+actor+action primary filters plus target/outcome narrowers");
  assert_true(result.records.front().event_id == "audit-export-001" &&
                  result.records.back().event_id == "audit-export-004",
              "AuditExporter should preserve stable timestamp/event_id ordering across primary and fallback records");
  assert_true(result.is_complete_page() && result.has_checksum(),
              "single-page exporter results should expose explicit final-page semantics and a checksum");
}

void test_audit_exporter_emits_resume_token_for_truncated_pages() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::ExportQuery;
  using dasall::infra::audit::AuditExporter;
  using dasall::tests::support::assert_true;

  const std::vector<dasall::infra::AuditEvent> primary_records{
      make_event("audit-export-page-001",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785601000),
      make_event("audit-export-page-002",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785602000),
      make_event("audit-export-page-003",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785603000),
  };

  const AuditExporter exporter(&primary_records, nullptr, 2U);
  const ExportQuery first_page_query{
      .start_ts = 1711785600000,
      .end_ts = 1711785605000,
      .actor = std::string("runtime"),
      .action = std::string("diagnostics.export"),
      .target = std::string("support-bundle"),
      .outcome = AuditOutcome::Succeeded,
      .page_token = std::string(),
  };

  const auto first_page = exporter.export_records(first_page_query);
  assert_true(first_page.records.size() == 2 && first_page.truncated,
              "AuditExporter should emit a truncated first page when the internal page size is smaller than the matching record set");
  assert_true(!first_page.next_page_token.empty(),
              "truncated exporter pages should carry an opaque resume token");

  const ExportQuery second_page_query{
      .start_ts = first_page_query.start_ts,
      .end_ts = first_page_query.end_ts,
      .actor = first_page_query.actor,
      .action = first_page_query.action,
      .target = first_page_query.target,
      .outcome = first_page_query.outcome,
      .page_token = first_page.next_page_token,
  };

  const auto second_page = exporter.export_records(second_page_query);
  assert_true(second_page.records.size() == 1,
              "resuming with the exporter token should continue from the next stable record position");
  assert_true(second_page.records.front().event_id == "audit-export-page-003",
              "the resume token should point to the first record strictly after the previous page boundary");
  assert_true(second_page.is_complete_page(),
              "the resumed page should close the export once no more records remain");
}

void test_audit_exporter_rejects_resume_token_reuse_across_filter_shapes() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::ExportQuery;
  using dasall::infra::audit::AuditExporter;
  using dasall::tests::support::assert_true;

  const std::vector<dasall::infra::AuditEvent> primary_records{
      make_event("audit-export-token-001",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785601000),
      make_event("audit-export-token-002",
                 "runtime",
                 "diagnostics.export",
                 "support-bundle",
                 AuditOutcome::Succeeded,
                 1711785602000),
      make_event("audit-export-token-003",
                 "runtime",
                 "diagnostics.export",
                 "policy-bundle",
                 AuditOutcome::Succeeded,
                 1711785603000),
  };

  const AuditExporter exporter(&primary_records, nullptr, 1U);
  const ExportQuery first_page_query{
      .start_ts = 1711785600000,
      .end_ts = 1711785605000,
      .actor = std::string("runtime"),
      .action = std::string("diagnostics.export"),
      .target = std::string("support-bundle"),
      .outcome = AuditOutcome::Succeeded,
      .page_token = std::string(),
  };

  const auto first_page = exporter.export_records(first_page_query);
  assert_true(!first_page.next_page_token.empty(),
              "the first page should provide a token that is bound to the original filter tuple");

  const ExportQuery mismatched_query{
      .start_ts = first_page_query.start_ts,
      .end_ts = first_page_query.end_ts,
      .actor = std::string("runtime"),
      .action = std::string("diagnostics.export"),
      .target = std::string("policy-bundle"),
      .outcome = AuditOutcome::Succeeded,
      .page_token = first_page.next_page_token,
  };

  const auto mismatched_result = exporter.export_records(mismatched_query);
  assert_true(mismatched_result.records.empty(),
              "resume tokens should not be reusable across different target/outcome filter tuples");
  assert_true(mismatched_result.is_complete_page() && mismatched_result.has_checksum(),
              "token mismatches should collapse to an explicit empty final page rather than silently widening the export shape");
}

}  // namespace

int main() {
  try {
    test_export_query_freezes_time_window_and_filter_fields();
    test_export_query_rejects_missing_or_inverted_time_window();
    test_export_result_freezes_records_checksum_and_resume_fields();
    test_export_result_rejects_inconsistent_truncation_state();
    test_audit_exporter_applies_primary_and_extension_filters();
    test_audit_exporter_emits_resume_token_for_truncated_pages();
    test_audit_exporter_rejects_resume_token_reuse_across_filter_shapes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
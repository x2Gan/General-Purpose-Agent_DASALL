#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "audit/AuditExporterTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

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

}  // namespace

int main() {
  try {
    test_export_query_freezes_time_window_and_filter_fields();
    test_export_query_rejects_missing_or_inverted_time_window();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
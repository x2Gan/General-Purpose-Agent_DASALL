#include <exception>
#include <iostream>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/audit/IAuditLogger.h"
#include "../../../infra/include/logging/IAuditLinkAdapter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasOpaqueSelectorMember = requires {
  &T::opaque_selector;
};

template <typename T>
concept HasWriteAuditMember = requires {
  &T::write_audit;
};

template <typename T>
concept HasExportAuditMember = requires {
  &T::export_audit;
};

void test_audit_logger_interface_signatures_use_frozen_audit_objects() {
  using dasall::contracts::ResultCode;
  using dasall::infra::AuditContext;
  using dasall::infra::AuditEvent;
  using dasall::infra::AuditWriteOutcome;
  using dasall::infra::ExportQuery;
  using dasall::infra::ExportResult;
  using dasall::infra::audit::IAuditLogger;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IAuditLogger::write_audit),
                               AuditWriteOutcome (IAuditLogger::*)(const AuditEvent&, const AuditContext&)>);
  static_assert(std::is_same_v<decltype(&IAuditLogger::export_audit),
                               ExportResult (IAuditLogger::*)(const ExportQuery&)>);
  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.error_code), std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(ExportResult{}.records), std::vector<AuditEvent>>);

  const AuditWriteOutcome write_failure{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = ResultCode::ValidationFieldMissing,
  };

  assert_true(write_failure.has_consistent_state() && write_failure.is_failure(),
              "IAuditLogger write path should remain representable with the frozen AuditWriteOutcome failure semantics");
}

void test_audit_link_adapter_stays_separate_from_audit_storage_interface() {
  using dasall::infra::LogEvent;
  using dasall::infra::LogWriteResult;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::IAuditLinkAdapter;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IAuditLinkAdapter::attach_audit_ref),
                               LogWriteResult (IAuditLinkAdapter::*)(LogEvent&, const AuditRef&)>);
  static_assert(std::is_same_v<decltype(&IAuditLinkAdapter::report_link_failure),
                               void (IAuditLinkAdapter::*)(std::string_view)>);
  static_assert(!HasWriteAuditMember<IAuditLinkAdapter>);
  static_assert(!HasExportAuditMember<IAuditLinkAdapter>);

  const auto failure = LogWriteResult::failure(dasall::contracts::ResultCode::ValidationFieldMissing,
                                               "audit ref placeholder missing",
                                               "logging.attach_audit_ref",
                                               "IAuditLinkAdapter");
  assert_true(!failure.ok && failure.references_only_contract_error_types(),
              "IAuditLinkAdapter should keep link-failure observability inside the existing contracts error domain without taking over audit persistence methods");
}

void test_audit_logger_keeps_export_query_private_to_infra_boundary() {
  using dasall::infra::AuditContext;
  using dasall::infra::ExportQuery;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ExportQuery{}.start_ts), std::int64_t>);
  static_assert(std::is_same_v<decltype(ExportQuery{}.end_ts), std::int64_t>);
  static_assert(!HasOpaqueSelectorMember<ExportQuery>);

  const AuditContext context{};
  const ExportQuery valid_query{
      .start_ts = 1711785600000,
      .end_ts = 1711785601000,
      .actor = std::string("runtime"),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  };
  const ExportQuery invalid_query{};

  assert_true(context.uses_unknown_defaults(),
              "IAuditLogger write path should keep correlation anchors inside the frozen AuditContext unknown-default contract");
  assert_true(valid_query.has_ordered_window(),
              "IAuditLogger export path should accept the frozen time-window query shape");
  assert_true(!invalid_query.has_required_window(),
              "IAuditLogger export path should reject the retired no-window placeholder semantics");
}

}  // namespace

int main() {
  try {
    test_audit_logger_interface_signatures_use_frozen_audit_objects();
    test_audit_link_adapter_stays_separate_from_audit_storage_interface();
    test_audit_logger_keeps_export_query_private_to_infra_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
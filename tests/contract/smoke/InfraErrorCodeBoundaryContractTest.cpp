#include <array>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/InfraErrorCode.h"
#include "../../../infra/include/audit/AuditErrors.h"
#include "../../../infra/include/audit/IAuditRetention.h"
#include "../../../infra/include/audit/AuditTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_infra_error_code_maps_only_to_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::InfraErrorCode;
  using dasall::infra::InfraErrorMapping;
  using dasall::infra::map_infra_error_code;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(InfraErrorMapping{}.result_code), ResultCode>);

    constexpr std::array<InfraErrorCode, 8> kFrozenCodes{
      InfraErrorCode::ConfigInvalid,
      InfraErrorCode::SecretUnavailable,
      InfraErrorCode::LogQueueFull,
      InfraErrorCode::AuditWriteFail,
      InfraErrorCode::HealthProbeTimeout,
      InfraErrorCode::OTAVerifyFail,
      InfraErrorCode::OTARollbackFail,
      InfraErrorCode::OTABootConfirmTimeout,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_infra_error_code(code);
    assert_true(mapping.result_code == ResultCode::ValidationFieldMissing ||
                    mapping.result_code == ResultCode::ProviderTimeout ||
                    mapping.result_code == ResultCode::RuntimeRetryExhausted,
                "infra private errors should map only to existing contracts result codes");
  }
}

void test_infra_error_code_names_stay_private_to_infra_boundary() {
  using dasall::infra::InfraErrorCode;
  using dasall::infra::infra_error_code_name;
  using dasall::tests::support::assert_true;

    constexpr std::array<InfraErrorCode, 8> kFrozenCodes{
      InfraErrorCode::ConfigInvalid,
      InfraErrorCode::SecretUnavailable,
      InfraErrorCode::LogQueueFull,
      InfraErrorCode::AuditWriteFail,
      InfraErrorCode::HealthProbeTimeout,
      InfraErrorCode::OTAVerifyFail,
      InfraErrorCode::OTARollbackFail,
      InfraErrorCode::OTABootConfirmTimeout,
  };

  for (const auto code : kFrozenCodes) {
    const auto name = infra_error_code_name(code);
    assert_true(name.starts_with("INF_E_"),
                "infra private error names should remain inside INF_E_* local namespace");
  }
}

void test_audit_error_code_maps_only_to_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::audit::AuditErrorCode;
  using dasall::infra::audit::AuditErrorMapping;
  using dasall::infra::audit::map_audit_error_code;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<AuditErrorCode, 6> kFrozenAuditCodes{
      AuditErrorCode::InvalidEvent,
      AuditErrorCode::WriteFail,
      AuditErrorCode::FallbackFail,
      AuditErrorCode::ExportDenied,
      AuditErrorCode::ExportFail,
      AuditErrorCode::RetentionFail,
  };

  for (const auto code : kFrozenAuditCodes) {
    const auto mapping = map_audit_error_code(code);
    assert_true(mapping.result_code == ResultCode::ValidationFieldMissing ||
                    mapping.result_code == ResultCode::PolicyDenied ||
                    mapping.result_code == ResultCode::ProviderTimeout ||
                    mapping.result_code == ResultCode::RuntimeRetryExhausted,
                "audit private errors should map only to existing contracts result codes");
    assert_true(!mapping.reason.empty(),
                "each frozen audit error code should carry a non-empty mapping reason");
  }
}

void test_audit_error_code_names_stay_private_to_audit_boundary() {
  using dasall::infra::audit::AuditErrorCode;
  using dasall::infra::audit::audit_error_code_name;
  using dasall::tests::support::assert_true;

  constexpr std::array<AuditErrorCode, 6> kFrozenAuditCodes{
      AuditErrorCode::InvalidEvent,
      AuditErrorCode::WriteFail,
      AuditErrorCode::FallbackFail,
      AuditErrorCode::ExportDenied,
      AuditErrorCode::ExportFail,
      AuditErrorCode::RetentionFail,
  };

  for (const auto code : kFrozenAuditCodes) {
    const auto name = audit_error_code_name(code);
    assert_true(name.starts_with("INF_E_AUDIT_"),
                "audit private error names should remain inside INF_E_AUDIT_* local namespace");
  }
}

void test_audit_write_outcome_error_code_stays_inside_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::audit::AuditErrorCode;
  using dasall::infra::audit::map_audit_error_code;
  using dasall::infra::AuditWriteOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(AuditWriteOutcome{}.error_code),
                               std::optional<ResultCode>>);

  const AuditWriteOutcome validation_failure{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = map_audit_error_code(AuditErrorCode::InvalidEvent).result_code,
  };

  const AuditWriteOutcome runtime_failure{
      .accepted = true,
      .persisted = false,
      .fallback_used = true,
      .error_code = map_audit_error_code(AuditErrorCode::FallbackFail).result_code,
  };

  assert_true(validation_failure.has_consistent_state() && validation_failure.is_failure(),
              "audit write validation failures should remain representable with existing contracts result codes only");
  assert_true(runtime_failure.has_consistent_state() && runtime_failure.is_failure(),
              "audit write runtime failures should remain representable with existing contracts result codes only");
}

  void test_retention_outcome_error_code_stays_inside_existing_contract_result_codes() {
    using dasall::contracts::ResultCode;
    using dasall::infra::AuditArchiveAction;
    using dasall::infra::AuditCleanupEvidence;
    using dasall::infra::AuditCleanupTrigger;
    using dasall::infra::RetentionOutcome;
    using dasall::infra::audit::AuditErrorCode;
    using dasall::infra::audit::map_audit_error_code;
    using dasall::tests::support::assert_true;

    static_assert(std::is_same_v<decltype(RetentionOutcome{}.error_code),
                   std::optional<ResultCode>>);

    const std::int64_t cutoff_ts = 1712304000000;
    const RetentionOutcome success{
      .completed = true,
      .cutoff_ts = cutoff_ts,
      .scanned_records = 7,
      .archived_records = 7,
      .deleted_records = 7,
      .detail_ref = std::string("diag://infra/audit/retention/scheduled_cleanup"),
      .error_code = std::nullopt,
      .archive_action = AuditArchiveAction{
        .archive_ref = std::string("diag://infra/audit/retention/archive/batch-013"),
        .archived_records = 7,
        .archived_through_ts = cutoff_ts,
        .checksum = std::string("retention-batch-013"),
      },
      .cleanup_evidence = AuditCleanupEvidence{
        .trigger = AuditCleanupTrigger::Scheduled,
        .cleanup_ref = std::string("diag://infra/audit/retention/cleanup/run-013"),
        .archive_ref = std::string("diag://infra/audit/retention/archive/batch-013"),
        .deleted_records = 7,
        .deleted_through_ts = cutoff_ts,
      },
    };
    const RetentionOutcome failure{
      .completed = false,
      .cutoff_ts = cutoff_ts,
      .scanned_records = 7,
      .archived_records = 0,
      .deleted_records = 0,
      .detail_ref = std::string("diag://infra/audit/retention/failure"),
      .error_code = map_audit_error_code(AuditErrorCode::RetentionFail).result_code,
      .archive_action = std::nullopt,
      .cleanup_evidence = std::nullopt,
    };

    assert_true(success.has_consistent_state() && success.is_success(),
          "retention success outcomes should remain representable without introducing new cross-contract error codes");
    assert_true(failure.has_consistent_state() && failure.is_failure(),
          "retention failure outcomes should stay mapped to existing contracts result codes only");
  }

}  // namespace

int main() {
  try {
    test_infra_error_code_maps_only_to_existing_contract_result_codes();
    test_infra_error_code_names_stay_private_to_infra_boundary();
    test_audit_error_code_maps_only_to_existing_contract_result_codes();
    test_audit_error_code_names_stay_private_to_audit_boundary();
    test_audit_write_outcome_error_code_stays_inside_existing_contract_result_codes();
    test_retention_outcome_error_code_stays_inside_existing_contract_result_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
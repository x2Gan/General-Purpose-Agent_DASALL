#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "policy/PolicyErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_policy_error_code_names_and_mapping_are_stable() {
  using dasall::infra::policy::map_policy_error_code;
  using dasall::infra::policy::policy_error_code_name;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::tests::support::assert_equal;

  assert_equal(std::string("INF_E_POLICY_BUNDLE_INVALID"),
               std::string(policy_error_code_name(PolicyErrorCode::BundleInvalid)),
               "policy bundle invalid code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_SCHEMA_UNSUPPORTED"),
               std::string(policy_error_code_name(PolicyErrorCode::SchemaUnsupported)),
               "policy schema unsupported code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_CONFLICT_UNRESOLVED"),
               std::string(policy_error_code_name(PolicyErrorCode::ConflictUnresolved)),
               "policy conflict unresolved code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_PATCH_BASE_MISMATCH"),
               std::string(policy_error_code_name(PolicyErrorCode::PatchBaseMismatch)),
               "policy patch base mismatch code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_SNAPSHOT_NOT_FOUND"),
               std::string(policy_error_code_name(PolicyErrorCode::SnapshotNotFound)),
               "policy snapshot not found code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_ROLLBACK_FAILED"),
               std::string(policy_error_code_name(PolicyErrorCode::RollbackFailed)),
               "policy rollback failed code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_QUERY_DENIED"),
               std::string(policy_error_code_name(PolicyErrorCode::QueryDenied)),
               "policy query denied code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_SOURCE_UNAVAILABLE"),
               std::string(policy_error_code_name(PolicyErrorCode::SourceUnavailable)),
               "policy source unavailable code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_STORE_COMMIT_FAILED"),
               std::string(policy_error_code_name(PolicyErrorCode::StoreCommitFailed)),
               "policy store commit failed code name should remain stable");
  assert_equal(std::string("INF_E_POLICY_DRYRUN_REJECTED"),
               std::string(policy_error_code_name(PolicyErrorCode::DryRunRejected)),
               "policy dry-run rejected code name should remain stable");

  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(map_policy_error_code(PolicyErrorCode::BundleInvalid).result_code),
               "policy bundle invalid should map to contracts validation category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::PolicyDenied),
               static_cast<int>(map_policy_error_code(PolicyErrorCode::ConflictUnresolved).result_code),
               "policy conflict unresolved should map to contracts policy category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
               static_cast<int>(map_policy_error_code(PolicyErrorCode::SourceUnavailable).result_code),
               "policy source unavailable should map to contracts provider category");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::RuntimeRetryExhausted),
               static_cast<int>(map_policy_error_code(PolicyErrorCode::StoreCommitFailed).result_code),
               "policy store commit failed should map to contracts runtime category");
}

void test_policy_error_code_mapping_covers_all_frozen_codes() {
  using dasall::contracts::classify_result_code;
  using dasall::contracts::ResultCodeCategory;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::tests::support::assert_true;

  constexpr std::array<PolicyErrorCode, 10> kFrozenCodes{
      PolicyErrorCode::BundleInvalid,
      PolicyErrorCode::SchemaUnsupported,
      PolicyErrorCode::ConflictUnresolved,
      PolicyErrorCode::PatchBaseMismatch,
      PolicyErrorCode::SnapshotNotFound,
      PolicyErrorCode::RollbackFailed,
      PolicyErrorCode::QueryDenied,
      PolicyErrorCode::SourceUnavailable,
      PolicyErrorCode::StoreCommitFailed,
      PolicyErrorCode::DryRunRejected,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_policy_error_code(code);
    const auto category = classify_result_code(mapping.result_code);

    assert_true(category == ResultCodeCategory::Validation ||
                    category == ResultCodeCategory::Policy ||
                    category == ResultCodeCategory::Provider ||
                    category == ResultCodeCategory::Runtime,
                "policy private error mapping should stay inside existing contracts categories");
    assert_true(!mapping.reason.empty(),
                "each frozen policy error mapping should carry an observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_policy_error_code_names_and_mapping_are_stable();
    test_policy_error_code_mapping_covers_all_frozen_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
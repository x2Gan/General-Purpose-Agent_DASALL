#include <array>
#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "policy/PolicyErrors.h"
#include "support/TestAssertions.h"

namespace {

void test_policy_error_code_maps_only_to_existing_contract_result_codes() {
  using dasall::contracts::ResultCode;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicyErrorMapping;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PolicyErrorMapping{}.result_code), ResultCode>);

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
    assert_true(mapping.result_code == ResultCode::ValidationFieldMissing ||
                    mapping.result_code == ResultCode::PolicyDenied ||
                    mapping.result_code == ResultCode::ProviderTimeout ||
                    mapping.result_code == ResultCode::RuntimeRetryExhausted,
                "policy private errors should map only to existing contracts result codes");
    assert_true(!mapping.reason.empty(),
                "each frozen policy error should carry a non-empty mapping reason");
  }
}

void test_policy_error_code_names_stay_private_to_policy_boundary() {
  using dasall::infra::policy::policy_error_code_name;
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
    const std::string_view name = policy_error_code_name(code);
    assert_true(name.starts_with("INF_E_POLICY_"),
                "policy private error names should remain inside INF_E_POLICY_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_policy_error_code_maps_only_to_existing_contract_result_codes();
    test_policy_error_code_names_stay_private_to_policy_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
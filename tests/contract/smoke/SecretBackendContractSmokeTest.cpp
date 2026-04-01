#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/secret/ISecretBackend.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasPlaintextField = requires(T value) {
  value.plaintext;
};

template <typename T>
concept HasKmsKeyIdField = requires(T value) {
  value.kms_key_id;
};

template <typename T>
concept HasFilePathField = requires(T value) {
  value.file_path;
};

void test_secret_backend_contract_keeps_backend_protocol_private_and_metadata_only() {
  using dasall::infra::secret::SecretBackendRecord;
  using dasall::infra::secret::SecretBackendState;
  using dasall::infra::secret::SecretBackendStatus;
  using dasall::infra::secret::SecretVersionPromotionRequest;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(SecretBackendRecord{}.backend_ref), std::string>);
  static_assert(std::is_same_v<decltype(SecretBackendStatus{}.state), SecretBackendState>);
  static_assert(std::is_same_v<decltype(SecretBackendStatus{}.last_error_code),
                               std::optional<dasall::contracts::ResultCode>>);
  static_assert(std::is_same_v<decltype(SecretVersionPromotionRequest{}.candidate_version),
                               std::string>);

  static_assert(!HasPlaintextField<SecretBackendRecord>);
  static_assert(!HasFilePathField<SecretBackendRecord>);
  static_assert(!HasKmsKeyIdField<SecretBackendRecord>);

  const SecretBackendRecord record{
      .descriptor = dasall::infra::secret::SecretDescriptor{
          .secret_name = std::string("db/root"),
          .backend_type = dasall::infra::secret::SecretBackendType::Mock,
          .classification = dasall::infra::secret::SecretClassification::Credential,
          .rotation_policy_ref = std::string("rotation/default"),
          .owner_ref = std::string("ops"),
      },
      .backend_ref = std::string("mock.primary"),
      .version = std::string("v3"),
      .cipher_ref = std::string("cipher://secret/001"),
      .encrypted_at_rest = true,
  };
  const SecretBackendStatus status{
      .state = SecretBackendState::Degraded,
      .backend_ref = std::string("mock.primary"),
      .rate_limited = true,
      .read_only_fallback_ready = true,
      .last_error_code = dasall::contracts::ResultCode::ProviderTimeout,
      .detail_ref = std::string("status://secret/backend/mock.primary"),
  };
  const SecretVersionPromotionRequest request{
      .secret_name = std::string("db/root"),
      .previous_version = std::string("v2"),
      .candidate_version = std::string("v3"),
      .rotation_epoch = 1,
      .validate_only = false,
  };

  assert_true(record.is_valid() && status.is_valid() && request.is_valid(),
              "secret backend contract should stay metadata-only while still exposing enough backend record, status, and promotion metadata for file/mock/kms parity");
}

void test_secret_backend_fetch_failures_cross_boundary_only_with_contract_error_types() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretBackendFetchResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(SecretBackendFetchResult{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(SecretBackendFetchResult{}.error_info),
                               std::optional<ErrorInfo>>);

  const auto failure = SecretBackendFetchResult::failure(
      ResultCode::ProviderTimeout,
      std::string("backend fetch timed out"),
      std::string("secret.fetch_record"),
      std::string("ISecretBackend"));

  assert_true(failure.is_valid() && failure.references_only_contract_error_types(),
              "secret backend fetch failures should cross the boundary only through contracts ResultCode and ErrorInfo");
}

}  // namespace

int main() {
  try {
    test_secret_backend_contract_keeps_backend_protocol_private_and_metadata_only();
    test_secret_backend_fetch_failures_cross_boundary_only_with_contract_error_types();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/secret/SecretTypes.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasPlaintextField = requires(T value) {
  value.plaintext;
};

template <typename T>
concept HasSecretValueField = requires(T value) {
  value.secret_value;
};

template <typename T>
concept HasFilePathField = requires(T value) {
  value.file_path;
};

template <typename T>
concept HasKmsKeyIdField = requires(T value) {
  value.kms_key_id;
};

void test_secret_types_keep_private_models_outside_shared_contract_objects() {
  using dasall::infra::secret::SecretAccessContext;
  using dasall::infra::secret::SecretBackendType;
  using dasall::infra::secret::SecretDescriptor;
  using dasall::infra::secret::SecretHandle;
  using dasall::infra::secret::SecretLease;
  using dasall::infra::secret::SecretQuery;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(SecretAccessContext{}.request_id),
                               std::optional<std::string>>);
  static_assert(std::is_same_v<decltype(SecretAccessContext{}.session_id),
                               std::optional<std::string>>);
  static_assert(std::is_same_v<decltype(SecretAccessContext{}.task_id),
                               std::optional<std::string>>);
  static_assert(std::is_same_v<decltype(SecretDescriptor{}.backend_type), SecretBackendType>);
  static_assert(std::is_same_v<decltype(SecretHandle{}.handle_id), std::string>);
  static_assert(std::is_same_v<decltype(SecretLease{}.lease_id), std::string>);

  static_assert(!HasPlaintextField<SecretQuery>);
  static_assert(!HasPlaintextField<SecretDescriptor>);
  static_assert(!HasPlaintextField<SecretHandle>);
  static_assert(!HasPlaintextField<SecretLease>);
  static_assert(!HasSecretValueField<SecretQuery>);
  static_assert(!HasSecretValueField<SecretDescriptor>);
  static_assert(!HasSecretValueField<SecretHandle>);
  static_assert(!HasFilePathField<SecretDescriptor>);
  static_assert(!HasFilePathField<SecretHandle>);
  static_assert(!HasKmsKeyIdField<SecretDescriptor>);
  static_assert(!HasKmsKeyIdField<SecretHandle>);

  assert_true(true,
              "secret types should remain infra-private models and should not collapse into shared contracts objects or backend-specific payloads");
}

void test_secret_result_types_cross_boundary_only_with_contract_error_payloads() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::RotationResult;
  using dasall::infra::secret::SecretHandleResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(RotationResult{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(RotationResult{}.error_info),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(SecretHandleResult{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(SecretHandleResult{}.error_info),
                               std::optional<ErrorInfo>>);

  const auto handle_failure = SecretHandleResult::failure(
      ResultCode::PolicyDenied,
      std::string("secret handle access denied"),
      std::string("secret.get_secret"),
      std::string("ISecretManager"));
  const auto rotation_failure = RotationResult::failure(
      std::string("db/root"),
      std::string("v2"),
      std::string("v2"),
      std::string("audit://secret/rotation/failed"),
      ResultCode::ProviderTimeout,
      std::string("backend timeout"),
      std::string("secret.rotate"),
      std::string("SecretRotationCoordinator"),
      true);

  assert_true(handle_failure.references_only_contract_error_types() && handle_failure.is_valid(),
              "secret handle failures should cross the boundary only through contracts ResultCode and ErrorInfo");
  assert_true(rotation_failure.references_only_contract_error_types() && rotation_failure.is_valid(),
              "secret rotation failures should cross the boundary only through contracts ResultCode and ErrorInfo");
}

}  // namespace

int main() {
  try {
    test_secret_types_keep_private_models_outside_shared_contract_objects();
    test_secret_result_types_cross_boundary_only_with_contract_error_payloads();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
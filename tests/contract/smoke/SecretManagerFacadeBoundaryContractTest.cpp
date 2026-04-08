#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

#include "secret/SecretManagerFacade.h"
#include "secret/backends/MockSecretBackend.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasRequestIdField = requires(T value) {
  value.request_id;
};

template <typename T>
concept HasTaskIdField = requires(T value) {
  value.task_id;
};

template <typename T>
concept HasSessionIdField = requires(T value) {
  value.session_id;
};

[[nodiscard]] dasall::infra::secret::MockSecretRecord make_secret_record() {
  using dasall::infra::secret::MockSecretRecord;
  using dasall::infra::secret::SecretBackendType;
  using dasall::infra::secret::SecretClassification;

  return MockSecretRecord{
      .record = {
          .descriptor = {
              .secret_name = std::string("db/root"),
              .backend_type = SecretBackendType::Mock,
              .classification = SecretClassification::Credential,
              .rotation_policy_ref = std::string("rotation/default"),
              .owner_ref = std::string("ops"),
          },
          .backend_ref = std::string("mock.primary"),
          .version = std::string("v3"),
          .cipher_ref = std::string("cipher://mock/db/root/v3"),
          .encrypted_at_rest = true,
      },
      .materialized_text = std::string("root-password"),
      .allowed_permission_domains = {std::string("secret.read")},
  };
}

[[nodiscard]] dasall::infra::secret::SecretQuery make_query() {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = std::string("db/root"),
      .version_hint = std::string("v3"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
}

void test_secret_manager_facade_keeps_handles_and_leases_free_of_context_fields() {
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::infra::secret::SecretHandle;
  using dasall::infra::secret::SecretLease;
  using dasall::tests::support::assert_true;

  static_assert(!HasRequestIdField<SecretHandle>);
  static_assert(!HasTaskIdField<SecretHandle>);
  static_assert(!HasSessionIdField<SecretHandle>);
  static_assert(!HasRequestIdField<SecretLease>);
  static_assert(!HasTaskIdField<SecretLease>);
  static_assert(!HasSessionIdField<SecretLease>);

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());
  SecretManagerFacade manager(backend);

  const dasall::infra::secret::SecretAccessContext access_context{
      .request_id = std::string("req-001"),
      .session_id = std::string("session-001"),
      .task_id = std::string("task-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };

  const auto handle_result = manager.get_secret(make_query(), access_context);
  const auto materialized_result = manager.materialize(handle_result.handle, access_context);

  assert_true(handle_result.ok && materialized_result.ok && handle_result.is_valid() &&
                  materialized_result.is_valid(),
              "SecretManagerFacade should keep the access chain valid while returning only the frozen handle and lease metadata without copying request/task/session fields into those objects");
}

void test_secret_manager_facade_failure_boundary_uses_only_contract_error_payloads() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::tests::support::assert_true;

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());
  SecretManagerFacade manager(backend);

  const dasall::infra::secret::SecretAccessContext invalid_access_context{
      .request_id = std::nullopt,
      .session_id = std::string("session-001"),
      .task_id = std::nullopt,
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };

  const auto failure = manager.get_secret(make_query(), invalid_access_context);

  assert_true(!failure.ok && failure.is_valid() &&
                  failure.result_code == ResultCode::ValidationFieldMissing &&
                  failure.references_only_contract_error_types(),
              "SecretManagerFacade should keep access-context validation failures inside contracts ResultCode and ErrorInfo without inventing extra boundary fields");
}

}  // namespace

int main() {
  try {
    test_secret_manager_facade_keeps_handles_and_leases_free_of_context_fields();
    test_secret_manager_facade_failure_boundary_uses_only_contract_error_payloads();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
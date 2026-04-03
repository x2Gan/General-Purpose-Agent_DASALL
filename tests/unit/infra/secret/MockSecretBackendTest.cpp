#include <cstddef>
#include <exception>
#include <iostream>
#include <string>

#include "secret/backends/MockSecretBackend.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] std::string buffer_to_text(const dasall::infra::secret::SecureBuffer& buffer) {
  std::string plaintext;
  plaintext.reserve(buffer.bytes().size());
  for (const std::byte value : buffer.bytes()) {
    plaintext.push_back(static_cast<char>(value));
  }

  return plaintext;
}

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

[[nodiscard]] dasall::infra::secret::SecretQuery make_query(std::string secret_name) {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = std::move(secret_name),
      .version_hint = std::string("v3"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
}

[[nodiscard]] dasall::infra::secret::SecretAccessContext make_access_context(
    std::string permission_domain) {
  using dasall::infra::secret::SecretAccessContext;

  return SecretAccessContext{
      .request_id = std::string("req-001"),
      .session_id = std::nullopt,
      .task_id = std::string("task-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::move(permission_domain),
  };
}

void test_mock_secret_backend_supports_successful_fetch_and_materialize() {
  using dasall::infra::secret::MockSecretBackend;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MockSecretBackend backend;
  backend.upsert_secret(make_secret_record());

  const auto fetched = backend.fetch_record(make_query("db/root"));
  const auto materialized = backend.materialize_record(fetched.record,
                                                       make_access_context("secret.read"));
  const auto status = backend.get_backend_status();

  assert_true(fetched.ok && fetched.is_valid(),
              "MockSecretBackend should return a valid backend record for a seeded secret");
  assert_true(materialized.ok && materialized.is_valid() && materialized.materialized_secret != nullptr,
              "MockSecretBackend should materialize a seeded secret into a secure buffer and lease");
  assert_true(materialized.lease.is_valid() && materialized.lease.is_active(),
              "MockSecretBackend should issue an active lease for successful materialization");
  assert_equal(std::string("root-password"),
               buffer_to_text(*materialized.materialized_secret),
               "MockSecretBackend should return the seeded plaintext only through SecureBuffer bytes");
  assert_true(status.is_valid() && status.state == dasall::infra::secret::SecretBackendState::Available,
              "MockSecretBackend should report an available backend state after a successful fetch/materialize path");
}

void test_mock_secret_backend_returns_not_found_for_missing_records() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::tests::support::assert_true;

  MockSecretBackend backend;
  const auto fetched = backend.fetch_record(make_query("db/missing"));

  assert_true(!fetched.ok && fetched.is_valid() &&
                  fetched.result_code == ResultCode::ValidationFieldMissing,
              "MockSecretBackend should return the frozen not-found contracts mapping for missing records");
}

void test_mock_secret_backend_rejects_materialization_for_denied_permission_domains() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::tests::support::assert_true;

  MockSecretBackend backend;
  backend.upsert_secret(make_secret_record());

  const auto fetched = backend.fetch_record(make_query("db/root"));
  const auto materialized = backend.materialize_record(fetched.record,
                                                       make_access_context("secret.rotate"));

  assert_true(!materialized.ok && materialized.is_valid() &&
                  materialized.result_code == ResultCode::PolicyDenied,
              "MockSecretBackend should deny materialization when the permission domain is outside the seeded allowlist");
}

void test_mock_secret_backend_reports_backend_down_for_fetch_and_status() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::SecretBackendState;
  using dasall::tests::support::assert_true;

  MockSecretBackend backend;
  backend.upsert_secret(make_secret_record());
  backend.set_available(false);

  const auto fetched = backend.fetch_record(make_query("db/root"));
  const auto status = backend.get_backend_status();

  assert_true(!fetched.ok && fetched.is_valid() &&
                  fetched.result_code == ResultCode::ProviderTimeout,
              "MockSecretBackend should surface backend-down fetch failures through the frozen backend unavailable mapping");
  assert_true(status.is_valid() && status.state == SecretBackendState::Unavailable &&
                  status.last_error_code == ResultCode::ProviderTimeout,
              "MockSecretBackend should report backend-down status with the last backend unavailable result code");
}

}  // namespace

int main() {
  try {
    test_mock_secret_backend_supports_successful_fetch_and_materialize();
    test_mock_secret_backend_returns_not_found_for_missing_records();
    test_mock_secret_backend_rejects_materialization_for_denied_permission_domains();
    test_mock_secret_backend_reports_backend_down_for_fetch_and_status();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
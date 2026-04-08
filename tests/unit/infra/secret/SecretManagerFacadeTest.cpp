#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "secret/SecretManagerFacade.h"
#include "secret/SecureBuffer.h"
#include "secret/backends/MockSecretBackend.h"
#include "support/TestAssertions.h"

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

[[nodiscard]] dasall::infra::secret::SecretAccessContext make_access_context() {
  using dasall::infra::secret::SecretAccessContext;

  return SecretAccessContext{
      .request_id = std::string("req-001"),
      .session_id = std::nullopt,
      .task_id = std::string("task-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
}

void test_secret_manager_facade_walks_get_materialize_release_and_inspect_chain() {
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());

  SecretManagerFacade manager(backend);
  const auto handle_result = manager.get_secret(make_query(), make_access_context());
  const auto materialized_result = manager.materialize(handle_result.handle, make_access_context());
  const auto inspection_result = manager.inspect("db/root");

  assert_true(handle_result.ok && handle_result.is_valid(),
              "SecretManagerFacade should turn a backend record into a valid metadata-only handle");
  assert_true(materialized_result.ok && materialized_result.is_valid() &&
                  materialized_result.materialized_secret != nullptr,
              "SecretManagerFacade should materialize a valid handle through the backend and return a secure buffer plus lease");
  assert_equal(std::string("root-password"),
               buffer_to_text(*materialized_result.materialized_secret),
               "SecretManagerFacade should expose the backend materialization bytes only through SecureBuffer");
  assert_true(manager.active_lease_count() == 1U,
              "SecretManagerFacade should track one active lease after a successful materialize call");
  const auto released_result = manager.release(materialized_result.lease);
  assert_true(released_result.ok && released_result.is_valid() && manager.active_lease_count() == 0U,
              "SecretManagerFacade should release a tracked lease and drop it from the active lease set");
  assert_true(inspection_result.ok && inspection_result.is_valid() &&
                  manager.has_cached_descriptor("db/root"),
              "SecretManagerFacade should inspect cached backend metadata without leaking plaintext into the returned descriptor");
}

void test_secret_manager_facade_rejects_expired_handles_before_backend_materialization() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretHandle;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::tests::support::assert_true;

  SecretManagerFacade manager(std::make_shared<dasall::infra::secret::MockSecretBackend>());
  const SecretHandle expired_handle{
      .handle_id = std::string("secret-handle://db/root/v3"),
      .secret_name = std::string("db/root"),
      .version = std::string("v3"),
      .backend_ref = std::string("mock.primary"),
      .issued_at_ms = 10,
      .expires_at_ms = 11,
      .redaction_hint = std::string("redacted://secret/db/root"),
  };

  const auto materialized_result = manager.materialize(expired_handle, make_access_context());

  assert_true(!materialized_result.ok && materialized_result.is_valid() &&
                  materialized_result.result_code == ResultCode::RuntimeRetryExhausted &&
                  manager.active_lease_count() == 0U,
              "SecretManagerFacade should reject expired handles before attempting backend materialization and should not register an active lease");
}

void test_secret_manager_facade_rejects_stale_handles_when_backend_version_has_rotated() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::RotationRequest;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::infra::secret::SecretQuery;
  using dasall::infra::secret::SecretAccessMode;
  using dasall::tests::support::assert_true;

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());

  SecretManagerFacade manager(backend);
  const auto handle_result = manager.get_secret(make_query(), make_access_context());

  auto rotated_record = make_secret_record();
  rotated_record.record.version = std::string("v4");
  rotated_record.record.cipher_ref = std::string("cipher://mock/db/root/v4");
  rotated_record.materialized_text = std::string("root-password-v4");
  backend->upsert_secret(std::move(rotated_record));

  const auto materialized_result = manager.materialize(handle_result.handle, make_access_context());

  assert_true(!materialized_result.ok && materialized_result.is_valid() &&
                  materialized_result.result_code == ResultCode::RuntimeRetryExhausted &&
                  manager.active_lease_count() == 0U,
              "SecretManagerFacade should reject stale handles when the backend version has rotated and require callers to reacquire a fresh handle");
}

  void test_secret_manager_facade_delegates_rotate_to_rotation_coordinator() {
    using dasall::infra::secret::MockSecretBackend;
    using dasall::infra::secret::RotationRequest;
    using dasall::infra::secret::RotationStrategy;
    using dasall::infra::secret::SecretManagerFacade;
    using dasall::infra::secret::SecretQuery;
    using dasall::infra::secret::SecretAccessMode;
    using dasall::tests::support::assert_true;

    auto backend = std::make_shared<MockSecretBackend>();
    backend->upsert_secret(make_secret_record());

    SecretManagerFacade manager(backend);
    const auto rotation_result = manager.rotate(RotationRequest{
      .secret_name = std::string("db/root"),
      .requested_by = std::string("ops-user"),
      .reason_code = std::string("scheduled_rotation"),
        .strategy = RotationStrategy::DualSlot,
      .validate_only = false,
    });

    const auto rotated_record = backend->fetch_record(SecretQuery{
      .secret_name = std::string("db/root"),
      .version_hint = std::string("v4"),
      .purpose = std::string("rotate"),
      .access_mode = SecretAccessMode::Rotate,
    });

    assert_true(rotation_result.rotated && rotation_result.is_valid() &&
              rotation_result.current_version == "v4" && rotation_result.rollback_ready &&
              rotated_record.ok && manager.rotation_status().rotation_backlog == 1,
          "SecretManagerFacade should delegate rotate requests to SecretRotationCoordinator instead of returning the old deferred failure placeholder");
  }

}  // namespace

int main() {
  try {
    test_secret_manager_facade_walks_get_materialize_release_and_inspect_chain();
    test_secret_manager_facade_rejects_expired_handles_before_backend_materialization();
    test_secret_manager_facade_rejects_stale_handles_when_backend_version_has_rotated();
    test_secret_manager_facade_delegates_rotate_to_rotation_coordinator();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
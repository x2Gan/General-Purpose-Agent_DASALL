#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "secret/SecretManagerFacade.h"
#include "secret/backends/MockSecretBackend.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

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

[[nodiscard]] dasall::infra::secret::SecretQuery make_query(std::string version_hint = {}) {
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretQuery;

  return SecretQuery{
      .secret_name = std::string("db/root"),
      .version_hint = std::move(version_hint),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
}

[[nodiscard]] dasall::infra::secret::SecretAccessContext make_access_context() {
  using dasall::infra::secret::SecretAccessContext;

  return SecretAccessContext{
      .request_id = std::string("req-rotation-001"),
      .session_id = std::nullopt,
      .task_id = std::string("task-rotation-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
}

void test_secret_rotation_workflow_rejects_stale_handles_after_dual_slot_rotation() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::RotationRequest;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretManagerFacade;
  using dasall::tests::support::assert_true;

  auto backend = std::make_shared<MockSecretBackend>();
  backend->upsert_secret(make_secret_record());

  SecretManagerFacade manager(backend);
  const auto old_handle = manager.get_secret(make_query("v3"), make_access_context());
  const auto rotation_result = manager.rotate(RotationRequest{
      .secret_name = std::string("db/root"),
      .requested_by = std::string("ops-user"),
      .reason_code = std::string("scheduled_rotation"),
      .strategy = RotationStrategy::DualSlot,
      .validate_only = false,
  });
  const auto stale_materialize = manager.materialize(old_handle.handle, make_access_context());
  const auto new_handle = manager.get_secret(make_query("v4"), make_access_context());

  assert_true(old_handle.ok && old_handle.is_valid(),
              "SecretRotationWorkflowTest should acquire an initial v3 handle before exercising the rotation workflow");
  assert_true(rotation_result.rotated && rotation_result.is_valid() &&
                  rotation_result.current_version == "v4" &&
                  manager.rotation_status().rotation_backlog == 1,
              "SecretRotationWorkflowTest should promote the rotated candidate to v4 and leave a dual-slot revoke backlog for the grace window");
  assert_true(!stale_materialize.ok && stale_materialize.is_valid() &&
                  stale_materialize.result_code == ResultCode::RuntimeRetryExhausted,
              "SecretRotationWorkflowTest should reject stale v3 handles after the rotation workflow has promoted a new current version");
  assert_true(new_handle.ok && new_handle.is_valid() && new_handle.handle.version == "v4",
              "SecretRotationWorkflowTest should allow callers to reacquire a fresh v4 handle after rotation completes");
}

}  // namespace

int main() {
  try {
    test_secret_rotation_workflow_rejects_stale_handles_after_dual_slot_rotation();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
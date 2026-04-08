#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "secret/SecretTypes.h"
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

void test_secret_types_freeze_core_secret_models_without_plaintext_fields() {
  using dasall::infra::secret::RotationRequest;
  using dasall::infra::secret::RotationResult;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretAccessContext;
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretAuditAction;
  using dasall::infra::secret::SecretAuditEvent;
  using dasall::infra::secret::SecretBackendType;
  using dasall::infra::secret::SecretClassification;
  using dasall::infra::secret::SecretDescriptor;
  using dasall::infra::secret::SecretHandle;
  using dasall::infra::secret::SecretHandleResult;
  using dasall::infra::secret::SecretInspectionResult;
  using dasall::infra::secret::SecretLease;
  using dasall::infra::secret::SecretLeaseState;
  using dasall::infra::secret::SecretLifecycleResult;
  using dasall::infra::secret::SecretQuery;
  using dasall::tests::support::assert_true;

  const SecretQuery query{
      .secret_name = std::string("db/root"),
      .version_hint = std::string("v3"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
  assert_true(query.is_valid(),
              "secret query should require a name, a purpose, and an explicit access mode");

  const SecretAccessContext access_context{
      .request_id = std::string("req-001"),
      .session_id = std::string("session-001"),
      .task_id = std::string("task-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
  assert_true(access_context.is_valid(),
              "secret access context should require an audit anchor plus actor, module, and permission metadata");

  const SecretDescriptor descriptor{
      .secret_name = query.secret_name,
      .backend_type = SecretBackendType::Mock,
      .classification = SecretClassification::Credential,
      .rotation_policy_ref = std::string("rotation/default"),
      .owner_ref = std::string("ops"),
  };
  assert_true(descriptor.is_valid(),
              "secret descriptor should freeze backend, classification, rotation policy, and owner metadata without carrying secret bytes");

  const SecretHandle handle{
      .handle_id = std::string("handle-001"),
      .secret_name = query.secret_name,
      .version = std::string("v3"),
      .backend_ref = std::string("mock.primary"),
      .issued_at_ms = 1000,
      .expires_at_ms = 2000,
      .redaction_hint = std::string("masked"),
  };
  assert_true(handle.is_valid(),
              "secret handle should stay metadata-only and should model a bounded lifetime with explicit issuance and expiry timestamps");

  const SecretLease lease{
      .lease_id = std::string("lease-001"),
      .handle_id = handle.handle_id,
      .consumer_ref = access_context.consumer_module,
      .expires_at_ms = 2000,
      .rotation_epoch = 1,
      .state = SecretLeaseState::Active,
  };
  assert_true(lease.is_valid() && lease.is_active(),
              "secret lease should freeze consumer ownership, expiry, rotation epoch, and active lifecycle state");

  const RotationRequest rotation_request{
      .secret_name = query.secret_name,
      .requested_by = access_context.actor,
      .reason_code = std::string("scheduled_rotation"),
      .strategy = RotationStrategy::DualSlot,
      .validate_only = false,
  };
  assert_true(rotation_request.is_valid(),
              "rotation request should require caller identity, reason, and an explicit strategy");

  const auto rotation_result = RotationResult::success(
      query.secret_name,
      std::string("v2"),
      std::string("v3"),
      std::string("audit://secret/rotation/001"),
      true);
  assert_true(rotation_result.is_valid(),
              "rotation result should freeze previous/current version transitions and rollback evidence on success");

  const SecretAuditEvent audit_event{
      .actor = access_context.actor,
      .action = SecretAuditAction::AccessGranted,
      .target_secret = query.secret_name,
      .consumer_module = access_context.consumer_module,
      .outcome = true,
      .reason_code = std::string("ok"),
      .version = std::string("v3"),
      .evidence_ref = std::string("audit://secret/access/001"),
      .request_id = access_context.request_id,
      .task_id = access_context.task_id,
  };
  assert_true(audit_event.is_valid(),
              "secret audit event should remain fully auditable without embedding plaintext payload fields");

  const auto handle_result = SecretHandleResult::success(handle);
  const auto inspection_result = SecretInspectionResult::success(descriptor);
  const auto lifecycle_result = SecretLifecycleResult::success(query.secret_name, lease.lease_id);

  assert_true(handle_result.is_valid(),
              "secret handle result should carry the frozen metadata-only handle on success");
  assert_true(inspection_result.is_valid(),
              "secret inspection result should carry the frozen descriptor on success");
  assert_true(lifecycle_result.is_valid(),
              "secret lifecycle result should carry the secret name and optional lease reference on success");

  static_assert(!HasPlaintextField<SecretQuery>);
  static_assert(!HasPlaintextField<SecretDescriptor>);
  static_assert(!HasPlaintextField<SecretHandle>);
  static_assert(!HasPlaintextField<SecretLease>);
  static_assert(!HasPlaintextField<RotationRequest>);
  static_assert(!HasPlaintextField<SecretAuditEvent>);
  static_assert(!HasSecretValueField<SecretQuery>);
  static_assert(!HasSecretValueField<SecretDescriptor>);
  static_assert(!HasSecretValueField<SecretHandle>);
}

void test_secret_types_reject_missing_anchors_invalid_lifetimes_and_incomplete_rotation_metadata() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::RotationRequest;
  using dasall::infra::secret::RotationResult;
  using dasall::infra::secret::SecretAccessContext;
  using dasall::infra::secret::SecretAccessMode;
  using dasall::infra::secret::SecretAuditEvent;
  using dasall::infra::secret::SecretDescriptor;
  using dasall::infra::secret::SecretHandle;
  using dasall::infra::secret::SecretLifecycleResult;
  using dasall::infra::secret::SecretQuery;
  using dasall::tests::support::assert_true;

  const SecretQuery invalid_query{
      .secret_name = std::string(""),
      .version_hint = std::string("v1"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = SecretAccessMode::Materialize,
  };
  assert_true(!invalid_query.is_valid(),
              "secret query should reject an empty secret name before any backend access starts");

  const SecretAccessContext invalid_context{
      .request_id = std::nullopt,
      .session_id = std::string("session-001"),
      .task_id = std::nullopt,
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };
  assert_true(!invalid_context.is_valid(),
              "secret access context should reject calls that carry no request_id or task_id audit anchor");

  const SecretDescriptor invalid_descriptor{
      .secret_name = std::string("db/root"),
      .rotation_policy_ref = std::string("rotation/default"),
      .owner_ref = std::string("ops"),
  };
  assert_true(!invalid_descriptor.is_valid(),
              "secret descriptor should reject missing backend type and classification metadata");

  const SecretHandle invalid_handle{
      .handle_id = std::string("handle-001"),
      .secret_name = std::string("db/root"),
      .version = std::string("v3"),
      .backend_ref = std::string("mock.primary"),
      .issued_at_ms = 2000,
      .expires_at_ms = 1000,
      .redaction_hint = std::string("masked"),
  };
  assert_true(!invalid_handle.is_valid(),
              "secret handle should reject expiry earlier than issue time so stale handles are observable");

  const RotationRequest invalid_rotation_request{
      .secret_name = std::string("db/root"),
      .requested_by = std::string("runtime"),
      .reason_code = std::string("scheduled_rotation"),
  };
  assert_true(!invalid_rotation_request.is_valid(),
              "rotation request should reject an unspecified strategy before rotation orchestration begins");

  const auto rotation_failure = RotationResult::failure(
      std::string("db/root"),
      std::string("v2"),
      std::string("v2"),
      std::string("audit://secret/rotation/failed"),
      ResultCode::ProviderTimeout,
      std::string("rotation validation backend timed out"),
      std::string("secret.rotate"),
      std::string("SecretRotationCoordinator"),
      true);
  assert_true(rotation_failure.is_valid() && rotation_failure.references_only_contract_error_types(),
              "rotation failure should map to contracts error payloads without inventing new shared contract objects");

  const SecretAuditEvent invalid_audit_event{
      .actor = std::string("runtime"),
      .target_secret = std::string("db/root"),
      .consumer_module = std::string("runtime"),
      .outcome = false,
      .reason_code = std::string("missing_anchor"),
      .version = std::string("v3"),
      .evidence_ref = std::string("audit://secret/access/failed"),
      .request_id = std::nullopt,
      .task_id = std::nullopt,
  };
  assert_true(!invalid_audit_event.is_valid(),
              "secret audit event should reject records that cannot be correlated back to a request or task anchor");

  const auto lifecycle_failure = SecretLifecycleResult::failure(
      std::string("db/root"),
      ResultCode::PolicyDenied,
      std::string("secret release denied by policy"),
      std::string("secret.release"),
      std::string("SecretManagerFacade"),
      std::string("lease-001"));
  assert_true(lifecycle_failure.is_valid() && lifecycle_failure.references_only_contract_error_types(),
              "secret lifecycle failures should remain observable through contracts ResultCode and ErrorInfo only");
}

}  // namespace

int main() {
  try {
    test_secret_types_freeze_core_secret_models_without_plaintext_fields();
    test_secret_types_reject_missing_anchors_invalid_lifetimes_and_incomplete_rotation_metadata();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
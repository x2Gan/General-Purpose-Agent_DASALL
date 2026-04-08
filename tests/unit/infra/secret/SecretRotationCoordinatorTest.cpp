#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "secret/SecretRotationCoordinator.h"
#include "secret/backends/MockSecretBackend.h"
#include "support/TestAssertions.h"

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
      .purpose = std::string("rotate"),
      .access_mode = SecretAccessMode::Rotate,
  };
}

[[nodiscard]] dasall::infra::secret::RotationRequest make_rotation_request(
    dasall::infra::secret::RotationStrategy strategy,
    bool validate_only = false) {
  using dasall::infra::secret::RotationRequest;

  return RotationRequest{
      .secret_name = std::string("db/root"),
      .requested_by = std::string("ops-user"),
      .reason_code = std::string("scheduled_rotation"),
      .strategy = strategy,
      .validate_only = validate_only,
  };
}

class RejectingRotationValidator final
    : public dasall::infra::secret::ISecretRotationValidator {
 public:
  [[nodiscard]] dasall::infra::secret::SecretRotationValidationDecision validate_candidate(
      const dasall::infra::secret::SecretRotationValidationContext& context) override {
    return dasall::infra::secret::SecretRotationValidationDecision::failure(
        dasall::contracts::ResultCode::ToolExecutionFailed,
        std::string("validator_rejected"),
        std::string("audit://secret/test/validation/rejected/") + context.secret_name,
        "forced validation rejection",
        "secret.rotate.validate",
        "RejectingRotationValidator",
        false);
  }
};

class ScriptedRotationBackend final : public dasall::infra::secret::ISecretBackend {
 public:
  explicit ScriptedRotationBackend(bool fail_revoke_once, bool fail_rollback_promote)
      : fail_revoke_once_(fail_revoke_once),
        fail_rollback_promote_(fail_rollback_promote) {
    delegate_.upsert_secret(make_secret_record());
  }

  [[nodiscard]] dasall::infra::secret::SecretBackendFetchResult fetch_record(
      const dasall::infra::secret::SecretQuery& query) override {
    return delegate_.fetch_record(query);
  }

  [[nodiscard]] dasall::infra::secret::SecretMaterializationResult materialize_record(
      const dasall::infra::secret::SecretBackendRecord& record,
      const dasall::infra::secret::SecretAccessContext& access_context) override {
    return delegate_.materialize_record(record, access_context);
  }

  [[nodiscard]] dasall::infra::secret::RotationResult promote_version(
      const dasall::infra::secret::SecretVersionPromotionRequest& request) override {
    ++promote_calls_;
    if (promote_calls_ > 1 && fail_rollback_promote_) {
      return dasall::infra::secret::RotationResult::failure(
          request.secret_name,
          request.previous_version,
          request.candidate_version,
          std::string("audit://secret/test/rollback_failed/forced"),
          dasall::contracts::ResultCode::RuntimeRetryExhausted,
          "forced rollback promote failure",
          "secret.promote_version",
          "ScriptedRotationBackend",
          false);
    }

    return delegate_.promote_version(request);
  }

  [[nodiscard]] dasall::infra::secret::SecretLifecycleResult revoke_version(
      std::string_view secret_name,
      std::string_view version) override {
    if (fail_revoke_once_ && !revoke_attempted_) {
      revoke_attempted_ = true;
      return dasall::infra::secret::SecretLifecycleResult::failure(
          std::string(secret_name),
          dasall::contracts::ResultCode::ToolExecutionFailed,
          "forced revoke failure",
          "secret.revoke_version",
          "ScriptedRotationBackend");
    }

    return delegate_.revoke_version(secret_name, version);
  }

  [[nodiscard]] dasall::infra::secret::SecretBackendStatus get_backend_status() const override {
    return delegate_.get_backend_status();
  }

 private:
  dasall::infra::secret::MockSecretBackend delegate_;
  bool fail_revoke_once_ = false;
  bool fail_rollback_promote_ = false;
  int promote_calls_ = 0;
  bool revoke_attempted_ = false;
};

void test_secret_rotation_coordinator_promotes_dual_slot_rotation_and_tracks_backlog() {
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretRotationCoordinator;
  using dasall::infra::secret::SecretRotationCoordinatorOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MockSecretBackend backend;
  backend.upsert_secret(make_secret_record());

  SecretRotationCoordinator coordinator(SecretRotationCoordinatorOptions{
      .dual_slot_enabled = true,
      .validation_required = true,
      .grace_period_sec = 600,
  });

  const auto result = coordinator.rotate(backend, make_rotation_request(RotationStrategy::DualSlot));
  const auto status = coordinator.get_status();
  const auto current_record = backend.fetch_record(make_query("v4"));

  assert_true(result.rotated && result.is_valid() && result.current_version == "v4" &&
                  result.rollback_ready,
              "SecretRotationCoordinator should validate and promote a dual-slot candidate while keeping the previous version in rollback-ready backlog during the grace window");
  assert_true(current_record.ok && current_record.record.version == "v4",
              "SecretRotationCoordinator should update the backend current version after a successful promotion");
  assert_equal(1, static_cast<int>(status.rotation_backlog),
               "SecretRotationCoordinator should report one pending revoke when dual-slot grace is enabled");
  assert_true(status.degraded && status.is_valid(),
              "SecretRotationCoordinator should expose a degraded backlog status while revoke cleanup is pending");
}

void test_secret_rotation_coordinator_rejects_validation_failures_without_promoting() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::MockSecretBackend;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretRotationCoordinator;
  using dasall::tests::support::assert_true;

  MockSecretBackend backend;
  backend.upsert_secret(make_secret_record());

  SecretRotationCoordinator coordinator({}, std::make_shared<RejectingRotationValidator>());

  const auto result = coordinator.rotate(backend, make_rotation_request(RotationStrategy::DualSlot));
  const auto status = coordinator.get_status();
  const auto previous_record = backend.fetch_record(make_query("v3"));

  assert_true(!result.rotated && result.is_valid() &&
                  result.result_code == ResultCode::ToolExecutionFailed &&
                  result.validation_state == dasall::infra::secret::RotationValidationState::Failed,
              "SecretRotationCoordinator should reject validator failures with the frozen rotation validation error semantics and keep the previous version current");
  assert_true(previous_record.ok && previous_record.record.version == "v3" &&
                  status.rotation_backlog == 0,
              "SecretRotationCoordinator should not promote the backend or create backlog when validation rejects the candidate version");
}

void test_secret_rotation_coordinator_rolls_back_when_revoke_fails() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretRotationCoordinator;
  using dasall::infra::secret::SecretRotationCoordinatorOptions;
  using dasall::tests::support::assert_true;

  ScriptedRotationBackend backend(true, false);
  SecretRotationCoordinator coordinator(SecretRotationCoordinatorOptions{
      .dual_slot_enabled = true,
      .validation_required = true,
      .grace_period_sec = 0,
  });

  const auto result = coordinator.rotate(backend, make_rotation_request(RotationStrategy::InPlace));
  const auto previous_record = backend.fetch_record(make_query("v3"));

  assert_true(!result.rotated && result.is_valid() &&
                  result.result_code == ResultCode::ToolExecutionFailed &&
                  result.validation_state == dasall::infra::secret::RotationValidationState::RolledBack,
              "SecretRotationCoordinator should roll back to the previous version when revoke cleanup fails after promote");
  assert_true(previous_record.ok && previous_record.record.version == "v3",
              "SecretRotationCoordinator should restore the previous backend version after a successful rollback");
}

void test_secret_rotation_coordinator_surfaces_rollback_failures_explicitly() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::RotationStrategy;
  using dasall::infra::secret::SecretRotationCoordinator;
  using dasall::infra::secret::SecretRotationCoordinatorOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScriptedRotationBackend backend(true, true);
  SecretRotationCoordinator coordinator(SecretRotationCoordinatorOptions{
      .dual_slot_enabled = true,
      .validation_required = true,
      .grace_period_sec = 0,
  });

  const auto result = coordinator.rotate(backend, make_rotation_request(RotationStrategy::InPlace));
  const auto status = coordinator.get_status();
  const auto candidate_record = backend.fetch_record(make_query("v4"));

  assert_true(!result.rotated && result.is_valid() &&
                  result.result_code == ResultCode::RuntimeRetryExhausted,
              "SecretRotationCoordinator should map rollback failures to the frozen rotation rollback error category");
  assert_true(candidate_record.ok && candidate_record.record.version == "v4",
              "SecretRotationCoordinator should leave the promoted candidate in place when rollback itself fails");
  assert_equal(1, static_cast<int>(status.rollback_failures),
               "SecretRotationCoordinator should track explicit rollback failure count for health and diagnostics");
  assert_true(status.degraded && status.is_valid(),
              "SecretRotationCoordinator should expose a degraded status after rollback failure exhaustion");
}

}  // namespace

int main() {
  try {
    test_secret_rotation_coordinator_promotes_dual_slot_rotation_and_tracks_backlog();
    test_secret_rotation_coordinator_rejects_validation_failures_without_promoting();
    test_secret_rotation_coordinator_rolls_back_when_revoke_fails();
    test_secret_rotation_coordinator_surfaces_rollback_failures_explicitly();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
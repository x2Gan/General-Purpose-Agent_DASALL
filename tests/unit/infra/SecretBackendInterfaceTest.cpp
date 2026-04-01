#include <exception>
#include <iostream>
#include <memory>
#include <string_view>
#include <type_traits>

#include "secret/ISecretBackend.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasGetSecretMethod = requires {
  &T::get_secret;
};

template <typename T>
concept HasRotateMethod = requires {
  &T::rotate;
};

template <typename T>
concept HasInspectMethod = requires {
  &T::inspect;
};

template <typename T>
concept HasSampleSecretHealthMethod = requires {
  &T::sample_secret_health;
};

class StaticSecretBackend final : public dasall::infra::secret::ISecretBackend {
 public:
  [[nodiscard]] dasall::infra::secret::SecretBackendFetchResult fetch_record(
      const dasall::infra::secret::SecretQuery& query) override {
    return dasall::infra::secret::SecretBackendFetchResult::success(
        dasall::infra::secret::SecretBackendRecord{
            .descriptor = dasall::infra::secret::SecretDescriptor{
                .secret_name = query.secret_name,
                .backend_type = dasall::infra::secret::SecretBackendType::Mock,
                .classification = dasall::infra::secret::SecretClassification::Credential,
                .rotation_policy_ref = std::string("rotation/default"),
                .owner_ref = std::string("ops"),
            },
            .backend_ref = std::string("mock.primary"),
            .version = std::string("v3"),
            .cipher_ref = std::string("cipher://secret/001"),
            .encrypted_at_rest = true,
        });
  }

  [[nodiscard]] dasall::infra::secret::SecretMaterializationResult materialize_record(
      const dasall::infra::secret::SecretBackendRecord& record,
      const dasall::infra::secret::SecretAccessContext& access_context) override {
    return dasall::infra::secret::SecretMaterializationResult::success(
        std::make_shared<dasall::infra::secret::SecureBuffer>(
            dasall::infra::secret::SecureBuffer::from_text_copy(record.descriptor.secret_name)),
        dasall::infra::secret::SecretLease{
            .lease_id = std::string("lease-001"),
            .handle_id = std::string("handle-001"),
            .consumer_ref = access_context.consumer_module,
            .expires_at_ms = 2000,
            .rotation_epoch = 1,
            .state = dasall::infra::secret::SecretLeaseState::Active,
        });
  }

  [[nodiscard]] dasall::infra::secret::RotationResult promote_version(
      const dasall::infra::secret::SecretVersionPromotionRequest& request) override {
    return dasall::infra::secret::RotationResult::success(
        request.secret_name,
        request.previous_version,
        request.candidate_version,
        std::string("audit://secret/promote/001"),
        true);
  }

  [[nodiscard]] dasall::infra::secret::SecretLifecycleResult revoke_version(
      std::string_view secret_name,
      std::string_view version) override {
    return dasall::infra::secret::SecretLifecycleResult::success(
        std::string(secret_name),
        std::string(version));
  }

  [[nodiscard]] dasall::infra::secret::SecretBackendStatus get_backend_status() const override {
    return dasall::infra::secret::SecretBackendStatus{
        .state = dasall::infra::secret::SecretBackendState::Available,
        .backend_ref = std::string("mock.primary"),
        .rate_limited = false,
        .read_only_fallback_ready = true,
        .last_error_code = std::nullopt,
        .detail_ref = std::string("status://secret/backend/mock.primary"),
    };
  }
};

void test_secret_backend_interface_freezes_shared_protocol_for_file_mock_and_kms_backends() {
  using dasall::infra::secret::ISecretBackend;
  using dasall::infra::secret::RotationResult;
  using dasall::infra::secret::SecretAccessContext;
  using dasall::infra::secret::SecretBackendFetchResult;
  using dasall::infra::secret::SecretBackendRecord;
  using dasall::infra::secret::SecretBackendStatus;
  using dasall::infra::secret::SecretLifecycleResult;
  using dasall::infra::secret::SecretMaterializationResult;
  using dasall::infra::secret::SecretQuery;
  using dasall::infra::secret::SecretVersionPromotionRequest;
  using dasall::tests::support::assert_true;

  using FetchSignature = SecretBackendFetchResult (ISecretBackend::*)(const SecretQuery&);
  using MaterializeSignature = SecretMaterializationResult (ISecretBackend::*)(
      const SecretBackendRecord&, const SecretAccessContext&);
  using PromoteSignature = RotationResult (ISecretBackend::*)(
      const SecretVersionPromotionRequest&);
  using RevokeSignature = SecretLifecycleResult (ISecretBackend::*)(
      std::string_view, std::string_view);
  using StatusSignature = SecretBackendStatus (ISecretBackend::*)() const;

  static_assert(std::is_same_v<decltype(&ISecretBackend::fetch_record), FetchSignature>);
  static_assert(std::is_same_v<decltype(&ISecretBackend::materialize_record), MaterializeSignature>);
  static_assert(std::is_same_v<decltype(&ISecretBackend::promote_version), PromoteSignature>);
  static_assert(std::is_same_v<decltype(&ISecretBackend::revoke_version), RevokeSignature>);
  static_assert(std::is_same_v<decltype(&ISecretBackend::get_backend_status), StatusSignature>);
  static_assert(std::is_abstract_v<ISecretBackend>);

  StaticSecretBackend backend;
  const SecretQuery query{
      .secret_name = std::string("db/root"),
      .version_hint = std::string("v3"),
      .purpose = std::string("runtime_bootstrap"),
      .access_mode = dasall::infra::secret::SecretAccessMode::Materialize,
  };
  const SecretAccessContext access_context{
      .request_id = std::string("req-001"),
      .session_id = std::nullopt,
      .task_id = std::string("task-001"),
      .actor = std::string("runtime"),
      .consumer_module = std::string("runtime"),
      .permission_domain = std::string("secret.read"),
  };

  const auto fetched = backend.fetch_record(query);
  const auto materialized = backend.materialize_record(fetched.record, access_context);
  const auto promoted = backend.promote_version(dasall::infra::secret::SecretVersionPromotionRequest{
      .secret_name = query.secret_name,
      .previous_version = std::string("v2"),
      .candidate_version = std::string("v3"),
      .rotation_epoch = 1,
      .validate_only = false,
  });
  const auto revoked = backend.revoke_version(query.secret_name, "v2");
  const auto status = backend.get_backend_status();

  assert_true(std::has_virtual_destructor_v<ISecretBackend>,
              "ISecretBackend should remain a pure abstract backend protocol with a virtual destructor");
  assert_true(fetched.is_valid() && materialized.is_valid() && promoted.is_valid() &&
                  revoked.is_valid() && status.is_valid(),
              "ISecretBackend should keep a stable fetch/materialize/promote/revoke/status protocol that file, mock, and kms backends can all implement");
}

void test_secret_backend_interface_does_not_absorb_consumer_or_health_entrypoints() {
  using dasall::infra::secret::ISecretBackend;
  using dasall::tests::support::assert_true;

  static_assert(!HasGetSecretMethod<ISecretBackend>);
  static_assert(!HasRotateMethod<ISecretBackend>);
  static_assert(!HasInspectMethod<ISecretBackend>);
  static_assert(!HasSampleSecretHealthMethod<ISecretBackend>);

  assert_true(!std::is_default_constructible_v<ISecretBackend>,
              "ISecretBackend should stay abstract and should not collapse into consumer-facing manager or health-source entrypoints");
}

}  // namespace

int main() {
  try {
    test_secret_backend_interface_freezes_shared_protocol_for_file_mock_and_kms_backends();
    test_secret_backend_interface_does_not_absorb_consumer_or_health_entrypoints();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
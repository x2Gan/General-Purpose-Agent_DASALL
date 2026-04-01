#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "ISecretManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasFetchRecordMethod = requires {
  &T::fetch_record;
};

template <typename T>
concept HasMaterializeRecordMethod = requires {
  &T::materialize_record;
};

template <typename T>
concept HasPromoteVersionMethod = requires {
  &T::promote_version;
};

template <typename T>
concept HasGetBackendStatusMethod = requires {
  &T::get_backend_status;
};

template <typename T>
concept HasSampleSecretHealthMethod = requires {
  &T::sample_secret_health;
};

void test_secret_manager_interface_keeps_six_frozen_entrypoints() {
  using dasall::infra::secret::ISecretManager;
  using dasall::infra::secret::RotationRequest;
  using dasall::infra::secret::RotationResult;
  using dasall::infra::secret::SecretAccessContext;
  using dasall::infra::secret::SecretHandle;
  using dasall::infra::secret::SecretHandleResult;
  using dasall::infra::secret::SecretInspectionResult;
  using dasall::infra::secret::SecretLease;
  using dasall::infra::secret::SecretLifecycleResult;
  using dasall::infra::secret::SecretMaterializationResult;
  using dasall::infra::secret::SecretQuery;
  using dasall::tests::support::assert_true;

  using GetSecretSignature = SecretHandleResult (ISecretManager::*)(
      const SecretQuery&, const SecretAccessContext&);
  using MaterializeSignature = SecretMaterializationResult (ISecretManager::*)(
      const SecretHandle&, const SecretAccessContext&);
  using ReleaseSignature = SecretLifecycleResult (ISecretManager::*)(
      const SecretLease&);
  using RotateSignature = RotationResult (ISecretManager::*)(
      const RotationRequest&);
  using RevokeSignature = SecretLifecycleResult (ISecretManager::*)(
      std::string_view, std::string_view);
  using InspectSignature = SecretInspectionResult (ISecretManager::*)(
      std::string_view) const;

  static_assert(std::is_same_v<decltype(&ISecretManager::get_secret),
                               GetSecretSignature>);
  static_assert(std::is_same_v<decltype(&ISecretManager::materialize),
                               MaterializeSignature>);
  static_assert(std::is_same_v<decltype(&ISecretManager::release),
                               ReleaseSignature>);
  static_assert(std::is_same_v<decltype(&ISecretManager::rotate),
                               RotateSignature>);
  static_assert(std::is_same_v<decltype(&ISecretManager::revoke),
                               RevokeSignature>);
  static_assert(std::is_same_v<decltype(&ISecretManager::inspect),
                               InspectSignature>);
  static_assert(std::is_abstract_v<ISecretManager>);

  assert_true(std::has_virtual_destructor_v<ISecretManager>,
              "ISecretManager should remain a pure abstract secret boundary with a virtual destructor");
  assert_true(!std::is_default_constructible_v<ISecretManager>,
              "ISecretManager should stay abstract until SecretManagerFacade is implemented");
}

void test_secret_manager_interface_does_not_absorb_backend_or_health_methods() {
  using dasall::infra::secret::ISecretManager;
  using dasall::tests::support::assert_true;

  static_assert(!HasFetchRecordMethod<ISecretManager>);
  static_assert(!HasMaterializeRecordMethod<ISecretManager>);
  static_assert(!HasPromoteVersionMethod<ISecretManager>);
  static_assert(!HasGetBackendStatusMethod<ISecretManager>);
  static_assert(!HasSampleSecretHealthMethod<ISecretManager>);

  assert_true(true,
              "ISecretManager should freeze only consumer-facing get/materialize/release/rotate/revoke/inspect entrypoints");
}

}  // namespace

int main() {
  try {
    test_secret_manager_interface_keeps_six_frozen_entrypoints();
    test_secret_manager_interface_does_not_absorb_backend_or_health_methods();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
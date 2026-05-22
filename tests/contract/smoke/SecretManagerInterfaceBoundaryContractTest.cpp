#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "../../../infra/include/secret/ISecretManager.h"
#include "support/TestAssertions.h"

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
concept HasRevokeVersionMethod = requires {
  &T::revoke_version;
};

template <typename T>
concept HasSampleSecretHealthMethod = requires {
  &T::sample_secret_health;
};

template <typename T>
concept HasCreateMethod = requires {
  &T::create;
};

template <typename T>
concept HasSetMethod = requires {
  &T::set;
};

template <typename T>
concept HasCreateSecretMethod = requires {
  &T::create_secret;
};

template <typename T>
concept HasSetSecretMethod = requires {
  &T::set_secret;
};

template <typename T>
concept HasProvisionMethod = requires {
  &T::provision;
};

template <typename T>
concept HasProvisionSecretMethod = requires {
  &T::provision_secret;
};

template <typename T>
concept HasImportMethod = requires {
  &T::import_secret;
};

template <typename T>
concept HasBootstrapMethod = requires {
  &T::bootstrap_secret;
};

void test_secret_manager_interface_keeps_consumer_boundary_without_backend_leakage() {
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

  assert_true(std::has_virtual_destructor_v<ISecretManager>,
              "ISecretManager should stay consumable through a pure abstract boundary");
  assert_true(!HasFetchRecordMethod<ISecretManager> &&
                  !HasMaterializeRecordMethod<ISecretManager> &&
                  !HasPromoteVersionMethod<ISecretManager> &&
                  !HasRevokeVersionMethod<ISecretManager> &&
            !HasSampleSecretHealthMethod<ISecretManager> &&
            !HasCreateMethod<ISecretManager> &&
            !HasSetMethod<ISecretManager> &&
            !HasCreateSecretMethod<ISecretManager> &&
            !HasSetSecretMethod<ISecretManager> &&
            !HasProvisionMethod<ISecretManager> &&
            !HasProvisionSecretMethod<ISecretManager> &&
            !HasImportMethod<ISecretManager> &&
            !HasBootstrapMethod<ISecretManager>,
          "ISecretManager should not absorb backend, health-probe, or bootstrap write protocols across the consumer boundary");
}

}  // namespace

int main() {
  try {
    test_secret_manager_interface_keeps_consumer_boundary_without_backend_leakage();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
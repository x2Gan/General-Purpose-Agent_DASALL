#include <exception>
#include <iostream>
#include <memory>
#include <string_view>

#include "AccessGatewayFactory.h"
#include "AccessOwnershipSecretWiring.h"
#include "RuntimeDependencySet.h"
#include "secret/ISecretManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

class FakeSecretManager final : public dasall::infra::secret::ISecretManager {
 public:
  [[nodiscard]] dasall::infra::secret::SecretHandleResult get_secret(
      const dasall::infra::secret::SecretQuery&,
      const dasall::infra::secret::SecretAccessContext&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::secret::SecretMaterializationResult materialize(
      const dasall::infra::secret::SecretHandle&,
      const dasall::infra::secret::SecretAccessContext&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::secret::SecretLifecycleResult release(
      const dasall::infra::secret::SecretLease&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::secret::RotationResult rotate(
      const dasall::infra::secret::RotationRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::secret::SecretLifecycleResult revoke(
      std::string_view,
      std::string_view) override {
    return {};
  }

  [[nodiscard]] dasall::infra::secret::SecretInspectionResult inspect(
      std::string_view) const override {
    return {};
  }
};

void daemon_access_options_receive_runtime_secret_manager() {
  auto dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
  auto secret_manager = std::make_shared<FakeSecretManager>();
  dependency_set->secret_manager = secret_manager;

  dasall::access::DaemonAccessPipelineOptions options;
  options.bootstrap_config.ownership_token_hmac_secret_ref =
      std::string("secret://access/receipt-hmac");

  dasall::apps::runtime_support::wire_runtime_secret_manager_into_access_ownership_seam(
      dependency_set,
      options);

  assert_true(options.ownership_secret_manager.get() == secret_manager.get(),
              "daemon app composition should project RuntimeDependencySet.secret_manager into the Access ownership seam");
}

void daemon_access_options_clear_secret_manager_when_dependency_set_is_missing() {
  dasall::access::DaemonAccessPipelineOptions options;
  options.ownership_secret_manager = std::make_shared<FakeSecretManager>();

  dasall::apps::runtime_support::wire_runtime_secret_manager_into_access_ownership_seam(
      std::shared_ptr<dasall::runtime::RuntimeDependencySet>{},
      options);

  assert_true(options.ownership_secret_manager == nullptr,
              "daemon app composition should not fabricate an ownership secret manager when runtime dependencies are absent");
}

}  // namespace

int main() {
  try {
    daemon_access_options_receive_runtime_secret_manager();
    daemon_access_options_clear_secret_manager_when_dependency_set_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
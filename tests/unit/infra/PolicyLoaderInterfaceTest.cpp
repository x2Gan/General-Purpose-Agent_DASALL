#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "policy/IPolicyLoader.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasLoadLayersMethod = requires {
  &T::load_layers;
};

template <typename T>
concept HasGetTypedMethod = requires {
  &T::get_typed;
};

template <typename T>
concept HasLoadSnapshotMethod = requires {
  &T::load_snapshot;
};

template <typename T>
concept HasActivateSnapshotMethod = requires {
  &T::activate_snapshot;
};

dasall::infra::policy::PolicyRuleDescriptor make_rule() {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::string("policy-loader-rule-001"),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("ops"),
      .action = std::string("apply_patch"),
      .target_selector = std::string("policy:*"),
      .effect = dasall::infra::policy::PolicyEffect::RequireConfirmation,
      .priority = 10,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("ticket_approved")},
      .reason_code = std::string("policy_admin_requires_confirmation"),
  };
}

class StaticPolicyLoader final : public dasall::infra::policy::IPolicyLoader {
 public:
  [[nodiscard]] dasall::infra::policy::PolicyBundle load_from_sources() override {
    return dasall::infra::policy::PolicyBundle{
        .bundle_id = std::string("policy-bundle-loader-001"),
        .schema_version = std::string("1"),
        .source = std::string(
            "source_id=infra/config/defaults/runtime_policy.yaml;version=defaults@1;"
            "profile_id=desktop_full"),
        .checksum = std::string("sha256:policy-loader-001"),
        .rules = {make_rule()},
        .generated_at = std::string("2026-04-01T14:30:00Z"),
    };
  }
};

void test_policy_loader_interface_keeps_policy_bundle_as_the_only_output_boundary() {
  using dasall::infra::policy::IPolicyLoader;
  using dasall::infra::policy::PolicyBundle;
  using dasall::tests::support::assert_true;

  using LoadFromSourcesSignature = PolicyBundle (IPolicyLoader::*)();

  static_assert(std::is_same_v<decltype(&IPolicyLoader::load_from_sources),
                               LoadFromSourcesSignature>);
  static_assert(std::is_same_v<decltype(PolicyBundle{}.source), std::string>);
  static_assert(std::is_same_v<decltype(PolicyBundle{}.schema_version), std::string>);
  static_assert(std::is_same_v<decltype(PolicyBundle{}.checksum), std::string>);
  static_assert(std::is_abstract_v<IPolicyLoader>);

  StaticPolicyLoader loader;
  const auto bundle = loader.load_from_sources();

  assert_true(std::has_virtual_destructor_v<IPolicyLoader>,
              "IPolicyLoader should remain a pure virtual boundary with a virtual destructor");
  assert_true(bundle.is_valid(),
              "IPolicyLoader should expose a valid PolicyBundle placeholder through the frozen interface");
  assert_true(bundle.source.find("source_id=") != std::string::npos &&
                  bundle.source.find("version=") != std::string::npos &&
                  bundle.checksum.rfind("sha256:", 0) == 0,
              "IPolicyLoader should keep source_id/version traceability and checksum inside the frozen PolicyBundle boundary");
}

void test_policy_loader_interface_does_not_absorb_config_center_or_runtime_provider_methods() {
  using dasall::infra::policy::IPolicyLoader;
  using dasall::tests::support::assert_true;

  static_assert(!HasLoadLayersMethod<IPolicyLoader>);
  static_assert(!HasGetTypedMethod<IPolicyLoader>);
  static_assert(!HasLoadSnapshotMethod<IPolicyLoader>);
  static_assert(!HasActivateSnapshotMethod<IPolicyLoader>);

  assert_true(!std::is_default_constructible_v<IPolicyLoader>,
              "IPolicyLoader should stay abstract and should not collapse into config-center or runtime-policy-provider responsibilities");
}

}  // namespace

int main() {
  try {
    test_policy_loader_interface_keeps_policy_bundle_as_the_only_output_boundary();
    test_policy_loader_interface_does_not_absorb_config_center_or_runtime_provider_methods();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
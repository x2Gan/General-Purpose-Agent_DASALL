#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/IConfigCenter.h"
#include "policy/PolicyLoader.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::config::TypedConfig make_typed_config(
    std::string key_path,
    dasall::infra::config::ConfigValueType value_type,
    std::string serialized_value,
    dasall::infra::config::ConfigSourceKind source_kind,
    std::string source_id) {
  return dasall::infra::config::TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string("1"),
      .source_kind = source_kind,
      .source_id = std::move(source_id),
      .secret_backed = false,
  };
}

class StaticConfigCenter final : public dasall::infra::config::IConfigCenter {
 public:
  explicit StaticConfigCenter(std::vector<dasall::infra::config::TypedConfig> typed_configs)
      : typed_configs_(std::move(typed_configs)) {}

  dasall::infra::config::ConfigApplyResult load_layers(
      const dasall::infra::config::ConfigStartupContext&) override {
    return dasall::infra::config::ConfigApplyResult::failure(
        dasall::contracts::ResultCode::RuntimeRetryExhausted,
        "load_layers is out of scope for PolicyLoaderBoundaryContractTest",
        "policy_loader_boundary.load_layers",
        "StaticConfigCenter");
  }

  std::optional<dasall::infra::config::TypedConfig> get_typed(
      const dasall::infra::config::ConfigQuery& query) const override {
    if (!query.is_valid()) {
      return std::nullopt;
    }

    for (const auto& typed_config : typed_configs_) {
      if (typed_config.key_path == query.key_path &&
          typed_config.value_type == query.expected_type) {
        return typed_config;
      }
    }

    return std::nullopt;
  }

  dasall::infra::config::ConfigApplyResult apply_override(
      const dasall::infra::config::ConfigPatch&) override {
    return dasall::infra::config::ConfigApplyResult::failure(
        dasall::contracts::ResultCode::RuntimeRetryExhausted,
        "apply_override is out of scope for PolicyLoaderBoundaryContractTest",
        "policy_loader_boundary.apply_override",
        "StaticConfigCenter");
  }

  dasall::infra::config::ConfigApplyResult rollback(
      const dasall::infra::config::ConfigRollbackToken&) override {
    return dasall::infra::config::ConfigApplyResult::failure(
        dasall::contracts::ResultCode::RuntimeRetryExhausted,
        "rollback is out of scope for PolicyLoaderBoundaryContractTest",
        "policy_loader_boundary.rollback",
        "StaticConfigCenter");
  }

  std::optional<dasall::infra::config::ConfigSubscriptionHandle> subscribe(
      const dasall::infra::config::ConfigSubscriptionRequest&) override {
    return std::nullopt;
  }

 private:
  std::vector<dasall::infra::config::TypedConfig> typed_configs_;
};

const dasall::infra::policy::PolicyRuleDescriptor* find_rule(
    const dasall::infra::policy::PolicyBundle& bundle,
    std::string_view action) {
  for (const auto& rule : bundle.rules) {
    if (rule.action == action) {
      return &rule;
    }
  }

  return nullptr;
}

void test_policy_loader_boundary_keeps_profile_clipping_inside_policy_admin_domain() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::policy::PolicyDomain;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyLoader;
  using dasall::tests::support::assert_true;

  StaticConfigCenter config_center({
      make_typed_config("profile_meta.profile_id",
                        ConfigValueType::String,
                        "edge_balanced",
                        ConfigSourceKind::Profile,
                        "profiles/edge_balanced/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.mode",
                        ConfigValueType::String,
                        "compat",
                        ConfigSourceKind::Profile,
                        "profiles/edge_balanced/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.hot_reload",
                        ConfigValueType::Boolean,
                        "true",
                        ConfigSourceKind::Profile,
                        "profiles/edge_balanced/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.default_effect",
                        ConfigValueType::String,
                        "allow",
                        ConfigSourceKind::Profile,
                        "profiles/edge_balanced/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.priority_order",
                        ConfigValueType::String,
                        "explicit-priority",
                        ConfigSourceKind::Profile,
                        "profiles/edge_balanced/runtime_policy.yaml"),
  });

  PolicyLoader loader(config_center);
  const auto bundle = loader.load_from_sources();
  const auto* patch_rule = find_rule(bundle, "apply_patch");

  assert_true(bundle.is_valid(),
              "PolicyLoader should expose a valid bundle boundary when profile clipping changes policy mode or default effect");
  assert_true(std::all_of(bundle.rules.begin(), bundle.rules.end(), [](const auto& rule) {
                return rule.domain == PolicyDomain::PolicyAdmin;
              }),
              "PolicyLoader should keep config-derived governance inside the policy_admin domain and must not emit runtime, prompt, or tool-owned domains");
  assert_true(patch_rule != nullptr &&
                  patch_rule->effect != PolicyEffect::Allow &&
                  patch_rule->effect != PolicyEffect::Observe &&
                  patch_rule->target_selector.rfind("policy:", 0) == 0,
              "PolicyLoader should not let profile clipping reopen the admin patch gate or redirect governance outside the policy-owned selector namespace");
}

void test_policy_loader_boundary_fails_closed_when_profile_disables_governance_inputs() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyLoader;
  using dasall::tests::support::assert_true;

  StaticConfigCenter config_center({
      make_typed_config("profile_meta.profile_id",
                        ConfigValueType::String,
                        "factory_test",
                        ConfigSourceKind::Profile,
                        "profiles/factory_test/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.enabled",
                        ConfigValueType::Boolean,
                        "false",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://factory/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.hot_reload",
                        ConfigValueType::Boolean,
                        "false",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://factory/policy-overlay.yaml"),
  });

  PolicyLoader loader(config_center);
  const auto bundle = loader.load_from_sources();
  const auto* patch_rule = find_rule(bundle, "apply_patch");

  assert_true(bundle.is_valid(),
              "PolicyLoader should keep a valid bundle boundary even when deployment inputs attempt to disable policy governance");
  assert_true(patch_rule != nullptr &&
                  patch_rule->effect == PolicyEffect::Deny &&
                  patch_rule->reason_code == "policy_loader_disabled_fail_closed",
              "PolicyLoader should fail closed on the admin patch gate when profile or deployment inputs disable governance so Audit and Runtime chains are not bypassed");
}

}  // namespace

int main() {
  try {
    test_policy_loader_boundary_keeps_profile_clipping_inside_policy_admin_domain();
    test_policy_loader_boundary_fails_closed_when_profile_disables_governance_inputs();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
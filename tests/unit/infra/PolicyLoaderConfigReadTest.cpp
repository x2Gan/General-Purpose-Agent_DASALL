#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "config/IConfigCenter.h"
#include "policy/PolicyLoader.h"
#include "support/TestAssertions.h"

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
        "load_layers is out of scope for PolicyLoaderConfigReadTest",
        "policy_loader_test.load_layers",
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
        "apply_override is out of scope for PolicyLoaderConfigReadTest",
        "policy_loader_test.apply_override",
        "StaticConfigCenter");
  }

  dasall::infra::config::ConfigApplyResult rollback(
      const dasall::infra::config::ConfigRollbackToken&) override {
    return dasall::infra::config::ConfigApplyResult::failure(
        dasall::contracts::ResultCode::RuntimeRetryExhausted,
        "rollback is out of scope for PolicyLoaderConfigReadTest",
        "policy_loader_test.rollback",
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

bool has_condition(const dasall::infra::policy::PolicyRuleDescriptor& rule,
                   std::string_view condition) {
  for (const auto& candidate : rule.conditions) {
    if (candidate == condition) {
      return true;
    }
  }

  return false;
}

void test_policy_loader_reads_strict_profile_defaults_and_traces_sources() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyLoader;
  using dasall::infra::policy::PolicyMode;
  using dasall::tests::support::assert_true;

  StaticConfigCenter config_center({
      make_typed_config("profile_meta.profile_id",
                        ConfigValueType::String,
                        "desktop_full",
                        ConfigSourceKind::Profile,
                        "profiles/desktop_full/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.enabled",
                        ConfigValueType::Boolean,
                        "true",
                        ConfigSourceKind::Defaults,
                        "infra/config/defaults/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.mode",
                        ConfigValueType::String,
                        "strict",
                        ConfigSourceKind::Defaults,
                        "infra/config/defaults/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.hot_reload",
                        ConfigValueType::Boolean,
                        "true",
                        ConfigSourceKind::Profile,
                        "profiles/desktop_full/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.max_history_snapshots",
                        ConfigValueType::UnsignedInteger,
                        "16",
                        ConfigSourceKind::Profile,
                        "profiles/desktop_full/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.default_effect",
                        ConfigValueType::String,
                        "deny",
                        ConfigSourceKind::Defaults,
                        "infra/config/defaults/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.priority_order",
                        ConfigValueType::String,
                        "deny-first",
                        ConfigSourceKind::Profile,
                        "profiles/desktop_full/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.require_checksum",
                        ConfigValueType::Boolean,
                        "true",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://site-001/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.dry_run_required",
                        ConfigValueType::Boolean,
                        "true",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://site-001/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.safe_mode_threshold",
                        ConfigValueType::UnsignedInteger,
                        "3",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://site-001/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.snapshot.persist_lkg",
                        ConfigValueType::Boolean,
                        "true",
                        ConfigSourceKind::Profile,
                        "profiles/desktop_full/runtime_policy.yaml"),
  });

  PolicyLoader loader(config_center);
  const auto bundle = loader.load_from_sources();
  const auto* patch_rule = find_rule(bundle, "apply_patch");
  const auto* default_rule = find_rule(bundle, "evaluate_default");

  assert_true(bundle.is_valid(),
              "PolicyLoader should build a valid PolicyBundle from the frozen config/profile policy keys");
  assert_true(bundle.source.find("profile_id=desktop_full") != std::string::npos &&
                  bundle.source.find("source_id=infra/config/defaults/runtime_policy.yaml") != std::string::npos &&
                  bundle.source.find("source_id=profiles/desktop_full/runtime_policy.yaml") != std::string::npos &&
                  bundle.source.find("source_id=deploy://site-001/policy-overlay.yaml") != std::string::npos,
              "PolicyLoader should keep profile_id and source_id traceability inside PolicyBundle::source");
  assert_true(bundle.checksum.rfind("loader-hash:", 0) == 0,
              "PolicyLoader should keep a deterministic non-empty checksum boundary on the resulting bundle");
  assert_true(patch_rule != nullptr && default_rule != nullptr,
              "PolicyLoader should materialize both the admin patch gate and the default-effect rule inside the loaded bundle");
  assert_true(patch_rule->effect == PolicyEffect::RequireConfirmation &&
                  patch_rule->mode == PolicyMode::Enforced &&
                  patch_rule->priority == 1 &&
                  has_condition(*patch_rule, "require_checksum=true") &&
                  has_condition(*patch_rule, "dry_run_required=true"),
              "PolicyLoader should reflect strict mode, deny-first priority, checksum, and dry-run requirements on the patch gate rule");
  assert_true(default_rule->effect == PolicyEffect::Deny &&
                  has_condition(*default_rule, "max_history_snapshots=16") &&
                  has_condition(*default_rule, "persist_lkg=true"),
              "PolicyLoader should propagate default_effect, max_history_snapshots, and persist_lkg into the default rule trace payload");
}

void test_policy_loader_reads_compat_alias_keys_and_disables_hot_reload_fail_closed() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyLoader;
  using dasall::infra::policy::PolicyMode;
  using dasall::tests::support::assert_true;

  StaticConfigCenter config_center({
      make_typed_config("profile_meta.profile_id",
                        ConfigValueType::String,
                        "edge_minimal",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
      make_typed_config("infra.security.policy.mode",
                        ConfigValueType::String,
                        "compat",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
      make_typed_config("infra.security.policy.hot_reload",
                        ConfigValueType::Boolean,
                        "false",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.default_effect",
                        ConfigValueType::String,
                        "allow",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.priority_order",
                        ConfigValueType::String,
                        "explicit-priority",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.max_history_snapshots",
                        ConfigValueType::UnsignedInteger,
                        "4",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.require_checksum",
                        ConfigValueType::Boolean,
                        "false",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://edge/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.dry_run_required",
                        ConfigValueType::Boolean,
                        "false",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://edge/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.safe_mode_threshold",
                        ConfigValueType::UnsignedInteger,
                        "5",
                        ConfigSourceKind::DeploymentOverride,
                        "deploy://edge/policy-overlay.yaml"),
      make_typed_config("infra.security_policy.snapshot.persist_lkg",
                        ConfigValueType::Boolean,
                        "false",
                        ConfigSourceKind::Profile,
                        "profiles/edge_minimal/runtime_policy.yaml"),
  });

  PolicyLoader loader(config_center);
  const auto bundle = loader.load_from_sources();
  const auto* patch_rule = find_rule(bundle, "apply_patch");
  const auto* default_rule = find_rule(bundle, "evaluate_default");

  assert_true(bundle.is_valid(),
              "PolicyLoader should stay valid when compat mode is sourced from the alias policy key path");
  assert_true(patch_rule != nullptr && default_rule != nullptr,
              "PolicyLoader should keep both loader rules even when hot_reload is disabled by profile clipping");
  assert_true(patch_rule->mode == PolicyMode::Compatibility &&
                  patch_rule->effect == PolicyEffect::Deny &&
                  patch_rule->priority == 100 &&
                  has_condition(*patch_rule, "hot_reload=false") &&
                  patch_rule->reason_code == "policy_hot_reload_disabled",
              "PolicyLoader should read compat mode from the alias key and fail closed on patch gating when hot_reload is disabled");
  assert_true(default_rule->effect == PolicyEffect::Allow &&
                  has_condition(*default_rule, "priority_order=explicit-priority") &&
                  has_condition(*default_rule, "persist_lkg=false"),
              "PolicyLoader should preserve explicit-priority and default_effect configuration on the default rule without reopening the admin patch gate");
}

void test_policy_loader_falls_back_to_frozen_defaults_when_values_are_missing_or_invalid() {
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::policy::PolicyEffect;
  using dasall::infra::policy::PolicyLoader;
  using dasall::infra::policy::PolicyMode;
  using dasall::tests::support::assert_true;

  StaticConfigCenter config_center({
      make_typed_config("profile_meta.profile_id",
                        ConfigValueType::String,
                        "cloud_full",
                        ConfigSourceKind::Profile,
                        "profiles/cloud_full/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.default_effect",
                        ConfigValueType::String,
                        "unexpected-value",
                        ConfigSourceKind::Profile,
                        "profiles/cloud_full/runtime_policy.yaml"),
      make_typed_config("infra.security_policy.priority_order",
                        ConfigValueType::String,
                        "unexpected-order",
                        ConfigSourceKind::Profile,
                        "profiles/cloud_full/runtime_policy.yaml"),
  });

  PolicyLoader loader(config_center);
  const auto bundle = loader.load_from_sources();
  const auto* patch_rule = find_rule(bundle, "apply_patch");
  const auto* default_rule = find_rule(bundle, "evaluate_default");

  assert_true(bundle.is_valid(),
              "PolicyLoader should still emit a valid bundle when policy config keys are missing or malformed");
  assert_true(bundle.source.find("source_id=infra/policy/defaults/frozen") != std::string::npos,
              "PolicyLoader should disclose synthetic frozen-default sources when policy keys are missing from ConfigCenter");
  assert_true(patch_rule != nullptr && patch_rule->mode == PolicyMode::Enforced &&
                  patch_rule->effect == PolicyEffect::RequireConfirmation,
              "PolicyLoader should fall back to strict mode and a confirmation-gated patch rule when compat/hot_reload inputs are unavailable");
  assert_true(default_rule != nullptr && default_rule->effect == PolicyEffect::Deny &&
                  default_rule->priority == 2,
              "PolicyLoader should normalize unknown default_effect and priority_order values back to deny-first semantics");
}

}  // namespace

int main() {
  try {
    test_policy_loader_reads_strict_profile_defaults_and_traces_sources();
    test_policy_loader_reads_compat_alias_keys_and_disables_hot_reload_fail_closed();
    test_policy_loader_falls_back_to_frozen_defaults_when_values_are_missing_or_invalid();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
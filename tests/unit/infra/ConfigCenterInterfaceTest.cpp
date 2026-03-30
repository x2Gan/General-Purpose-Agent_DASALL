#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "config/IConfigCenter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullConfigCenter final : public dasall::infra::config::IConfigCenter {
 public:
  NullConfigCenter()
      : baseline_config_{
            .key_path = std::string("infra.config.validation.strict"),
            .value_type = dasall::infra::config::ConfigValueType::Boolean,
            .serialized_value = std::string("true"),
            .schema_version = std::string("1"),
            .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
            .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
            .secret_backed = false,
        },
        active_config_(baseline_config_) {}

  dasall::infra::config::ConfigApplyResult load_layers(
      const dasall::infra::config::ConfigStartupContext& startup_context) override {
    if (!startup_context.is_valid()) {
      return dasall::infra::config::ConfigApplyResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config startup context must keep a supported profile_id and actor_ref",
          "config.load_layers",
          "NullConfigCenter");
    }

    loaded_ = true;
    active_config_ = baseline_config_;
    return dasall::infra::config::ConfigApplyResult::success("rollback://config/1");
  }

  std::optional<dasall::infra::config::TypedConfig> get_typed(
      const dasall::infra::config::ConfigQuery& query) const override {
    if (!loaded_ || !query.is_valid()) {
      return std::nullopt;
    }

    if (query.key_path != active_config_.key_path ||
        query.expected_type != active_config_.value_type) {
      return std::nullopt;
    }

    return active_config_;
  }

  dasall::infra::config::ConfigApplyResult apply_override(
      const dasall::infra::config::ConfigPatch& config_patch) override {
    if (!loaded_ || !config_patch.is_valid()) {
      return dasall::infra::config::ConfigApplyResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config override patch must stay inside the frozen typed runtime override contract",
          "config.apply_override",
          "NullConfigCenter");
    }

    active_config_ = *config_patch.patches.front().value;
    return dasall::infra::config::ConfigApplyResult::success("rollback://config/2");
  }

  dasall::infra::config::ConfigApplyResult rollback(
      const dasall::infra::config::ConfigRollbackToken& rollback_token) override {
    if (!rollback_token.is_valid()) {
      return dasall::infra::config::ConfigApplyResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config rollback token must stay explicit and attributable",
          "config.rollback",
          "NullConfigCenter");
    }

    active_config_ = baseline_config_;
    return dasall::infra::config::ConfigApplyResult::success(rollback_token.token);
  }

  std::optional<dasall::infra::config::ConfigSubscriptionHandle> subscribe(
      const dasall::infra::config::ConfigSubscriptionRequest& subscription_request) override {
    if (!subscription_request.is_valid()) {
      return std::nullopt;
    }

    return dasall::infra::config::ConfigSubscriptionHandle{
        .subscription_id = std::string("subscription-001"),
        .namespace_filter = subscription_request.namespace_filter,
        .subscriber_id = subscription_request.subscriber_id,
        .active = true,
    };
  }

 private:
  bool loaded_ = false;
  dasall::infra::config::TypedConfig baseline_config_;
  dasall::infra::config::TypedConfig active_config_;
};

void test_config_center_interface_accepts_valid_startup_lookup_and_subscription() {
  using dasall::infra::config::ConfigDefaultPolicy;
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigPatchEntry;
  using dasall::infra::config::ConfigPatchOperation;
  using dasall::infra::config::ConfigQuery;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigStartupContext;
  using dasall::infra::config::ConfigSubscriptionRequest;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::TypedConfig;
  using dasall::tests::support::assert_true;

  NullConfigCenter center;

  const auto load_result = center.load_layers(ConfigStartupContext{
      .requested_profile_id = std::string("desktop_full"),
      .deployment_source_ref = std::string("deploy://site-001/config.yaml"),
      .runtime_overlay_source_ref = std::string("ops://window/bootstrap"),
      .actor_ref = std::string("runtime-bootstrap"),
      .load_runtime_overlay = true,
  });
  assert_true(load_result.applied,
              "IConfigCenter skeleton should accept a startup context with a frozen profile_id and actor_ref");

  const auto lookup_result = center.get_typed(ConfigQuery{
      .key_path = std::string("infra.config.validation.strict"),
      .expected_type = ConfigValueType::Boolean,
      .default_policy = ConfigDefaultPolicy::FailIfMissing,
      .fallback_serialized_value = std::string(),
  });
  assert_true(lookup_result.has_value(),
              "IConfigCenter skeleton should return a typed config placeholder once layers are loaded");

  const auto apply_result = center.apply_override(ConfigPatch{
      .patch_id = std::string("runtime-patch-004"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/200"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 1,
      .reason_code = std::string("temporary_debug"),
      .expires_at = std::string("2026-03-30T16:00:00Z"),
      .patches = {ConfigPatchEntry{
          .op = ConfigPatchOperation::Replace,
          .key_path = std::string("infra.config.validation.strict"),
          .value = TypedConfig{
              .key_path = std::string("infra.config.validation.strict"),
              .value_type = ConfigValueType::Boolean,
              .serialized_value = std::string("false"),
              .schema_version = std::string("1"),
              .source_kind = ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/200"),
              .secret_backed = false,
          },
      }},
  });
  assert_true(apply_result.applied,
              "IConfigCenter skeleton should accept a fully specified runtime override patch placeholder");

  const auto subscription = center.subscribe(ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config."),
      .subscriber_id = std::string("runtime-provider"),
      .callback = [](const dasall::infra::config::ConfigDiff&) {},
  });
  assert_true(subscription.has_value() && subscription->is_valid() && subscription->active,
              "IConfigCenter skeleton should return an active subscription handle for a valid namespace-filtered callback request");
}

void test_config_center_interface_reports_invalid_inputs_observably() {
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigPatchEntry;
  using dasall::infra::config::ConfigPatchOperation;
  using dasall::infra::config::ConfigRollbackToken;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigStartupContext;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::TypedConfig;
  using dasall::tests::support::assert_true;

  NullConfigCenter center;

  const auto invalid_load = center.load_layers(ConfigStartupContext{
      .requested_profile_id = std::string("staging"),
      .deployment_source_ref = std::string("deploy://site-001/config.yaml"),
      .runtime_overlay_source_ref = std::string(),
      .actor_ref = std::string(),
      .load_runtime_overlay = false,
  });
  assert_true(!invalid_load.applied,
              "IConfigCenter skeleton should reject unsupported profile aliases or missing actor attribution");
  assert_true(invalid_load.references_only_contract_error_types(),
              "load_layers failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto load_result = center.load_layers(ConfigStartupContext{
      .requested_profile_id = std::string("desktop_full"),
      .deployment_source_ref = std::string("deploy://site-001/config.yaml"),
      .runtime_overlay_source_ref = std::string(),
      .actor_ref = std::string("runtime-bootstrap"),
      .load_runtime_overlay = false,
  });
  assert_true(load_result.applied,
              "IConfigCenter skeleton should still allow a baseline load before validating override and rollback failures");

  const auto invalid_patch = center.apply_override(ConfigPatch{
      .patch_id = std::string("runtime-patch-005"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/201"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 1,
      .reason_code = std::string("forbidden_profile_change"),
      .expires_at = std::string("2026-03-30T16:10:00Z"),
      .patches = {ConfigPatchEntry{
          .op = ConfigPatchOperation::Replace,
          .key_path = std::string("profile_meta.profile_id"),
          .value = TypedConfig{
              .key_path = std::string("profile_meta.profile_id"),
              .value_type = ConfigValueType::String,
              .serialized_value = std::string("edge_minimal"),
              .schema_version = std::string("1"),
              .source_kind = ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/201"),
              .secret_backed = false,
          },
      }},
  });
  assert_true(!invalid_patch.applied,
              "IConfigCenter skeleton should reject runtime overrides that target protected profile identity paths");
  assert_true(invalid_patch.references_only_contract_error_types(),
              "apply_override failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto invalid_rollback = center.rollback(ConfigRollbackToken{});
  assert_true(!invalid_rollback.applied,
              "IConfigCenter skeleton should reject unspecified rollback tokens");
  assert_true(invalid_rollback.references_only_contract_error_types(),
              "rollback failures should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_config_center_interface_accepts_valid_startup_lookup_and_subscription();
    test_config_center_interface_reports_invalid_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
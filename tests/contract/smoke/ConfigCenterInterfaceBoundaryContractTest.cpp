#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "../../../infra/include/config/IConfigCenter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class BoundaryConfigCenter final : public dasall::infra::config::IConfigCenter {
 public:
  dasall::infra::config::ConfigApplyResult load_layers(
      const dasall::infra::config::ConfigStartupContext& startup_context) override {
    if (!startup_context.is_valid()) {
      return dasall::infra::config::ConfigApplyResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config startup context is required",
          "config.load_layers",
          "BoundaryConfigCenter");
    }

    return dasall::infra::config::ConfigApplyResult::success("rollback://config/10");
  }

  std::optional<dasall::infra::config::TypedConfig> get_typed(
      const dasall::infra::config::ConfigQuery& query) const override {
    if (!query.is_valid()) {
      return std::nullopt;
    }

    return dasall::infra::config::TypedConfig{
        .key_path = query.key_path,
        .value_type = query.expected_type,
        .serialized_value = std::string("true"),
        .schema_version = std::string("1"),
        .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
        .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
        .secret_backed = false,
    };
  }

  dasall::infra::config::ConfigApplyResult apply_override(
      const dasall::infra::config::ConfigPatch& config_patch) override {
    if (!config_patch.is_valid()) {
      return dasall::infra::config::ConfigApplyResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "runtime override must remain inside the frozen typed patch contract",
          "config.apply_override",
          "BoundaryConfigCenter");
    }

    return dasall::infra::config::ConfigApplyResult::success("rollback://config/11");
  }

  dasall::infra::config::ConfigApplyResult rollback(
      const dasall::infra::config::ConfigRollbackToken& rollback_token) override {
    if (!rollback_token.is_valid()) {
      return dasall::infra::config::ConfigApplyResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "rollback token must stay explicit",
          "config.rollback",
          "BoundaryConfigCenter");
    }

    return dasall::infra::config::ConfigApplyResult::success(rollback_token.token);
  }

  std::optional<dasall::infra::config::ConfigSubscriptionHandle> subscribe(
      const dasall::infra::config::ConfigSubscriptionRequest& subscription_request) override {
    if (!subscription_request.is_valid()) {
      return std::nullopt;
    }

    return dasall::infra::config::ConfigSubscriptionHandle{
        .subscription_id = std::string("subscription-010"),
        .namespace_filter = subscription_request.namespace_filter,
        .subscriber_id = subscription_request.subscriber_id,
        .active = true,
    };
  }
};

void test_config_center_interface_signatures_stay_local_to_config() {
  using dasall::infra::config::ConfigApplyResult;
  using dasall::infra::config::ConfigChangedCallback;
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigQuery;
  using dasall::infra::config::ConfigRollbackToken;
  using dasall::infra::config::ConfigStartupContext;
  using dasall::infra::config::ConfigSubscriptionHandle;
  using dasall::infra::config::ConfigSubscriptionRequest;
  using dasall::infra::config::IConfigCenter;
  using dasall::infra::config::TypedConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<IConfigCenter&>().load_layers(
                                   std::declval<const ConfigStartupContext&>())),
                               ConfigApplyResult>);
  static_assert(std::is_same_v<decltype(std::declval<const IConfigCenter&>().get_typed(
                                   std::declval<const ConfigQuery&>())),
                               std::optional<TypedConfig>>);
  static_assert(std::is_same_v<decltype(std::declval<IConfigCenter&>().apply_override(
                                   std::declval<const ConfigPatch&>())),
                               ConfigApplyResult>);
  static_assert(std::is_same_v<decltype(std::declval<IConfigCenter&>().rollback(
                                   std::declval<const ConfigRollbackToken&>())),
                               ConfigApplyResult>);
  static_assert(std::is_same_v<decltype(std::declval<IConfigCenter&>().subscribe(
                                   std::declval<const ConfigSubscriptionRequest&>())),
                               std::optional<ConfigSubscriptionHandle>>);
  static_assert(std::is_same_v<decltype(ConfigSubscriptionRequest{}.callback), ConfigChangedCallback>);

  const ConfigSubscriptionRequest request{
      .namespace_filter = std::string("infra.config."),
      .subscriber_id = std::string("runtime-provider"),
      .callback = [](const dasall::infra::config::ConfigDiff&) {},
  };
  assert_true(request.is_valid(),
              "config subscribe request should keep namespace_filter, subscriber_id, and callback as the minimal local contract");
}

void test_config_center_interface_rejects_profile_protected_runtime_overrides() {
  using dasall::infra::config::ConfigPatch;
  using dasall::infra::config::ConfigPatchEntry;
  using dasall::infra::config::ConfigPatchOperation;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::config::TypedConfig;
  using dasall::tests::support::assert_true;

  BoundaryConfigCenter center;
  const auto result = center.apply_override(ConfigPatch{
      .patch_id = std::string("runtime-patch-006"),
      .source_kind = ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/202"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 3,
      .reason_code = std::string("attempt_module_toggle"),
      .expires_at = std::string("2026-03-30T17:00:00Z"),
      .patches = {ConfigPatchEntry{
          .op = ConfigPatchOperation::Replace,
          .key_path = std::string("enabled_modules.runtime"),
          .value = TypedConfig{
              .key_path = std::string("enabled_modules.runtime"),
              .value_type = ConfigValueType::Boolean,
              .serialized_value = std::string("false"),
              .schema_version = std::string("1"),
              .source_kind = ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/202"),
              .secret_backed = false,
          },
      }},
  });

  assert_true(!result.applied,
              "config interface boundary should reject runtime overrides that try to bypass profile-owned enabled_modules keys");
  assert_true(result.references_only_contract_error_types(),
              "config interface override denials should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_config_center_interface_signatures_stay_local_to_config();
    test_config_center_interface_rejects_profile_protected_runtime_overrides();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
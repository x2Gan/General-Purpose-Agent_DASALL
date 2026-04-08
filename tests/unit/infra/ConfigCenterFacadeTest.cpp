#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "config/ConfigCenterFacade.h"
#include "config/ConfigErrors.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::config::ConfigStartupContext make_startup_context(bool load_runtime_overlay = true) {
  return dasall::infra::config::ConfigStartupContext{
      .requested_profile_id = std::string("desktop_full"),
      .deployment_source_ref = std::string("deploy://site-001/config.yaml"),
      .runtime_overlay_source_ref =
          load_runtime_overlay ? std::string("ops://window/bootstrap") : std::string(),
      .actor_ref = std::string("runtime-bootstrap"),
      .load_runtime_overlay = load_runtime_overlay,
  };
}

dasall::infra::config::ConfigQuery make_boolean_query(std::string key_path) {
  return dasall::infra::config::ConfigQuery{
      .key_path = std::move(key_path),
      .expected_type = dasall::infra::config::ConfigValueType::Boolean,
      .default_policy = dasall::infra::config::ConfigDefaultPolicy::FailIfMissing,
      .fallback_serialized_value = std::string(),
  };
}

dasall::infra::config::ConfigPatch make_runtime_override_patch(std::uint64_t base_version,
                                                               std::string serialized_value) {
  return dasall::infra::config::ConfigPatch{
      .patch_id = std::string("runtime-patch-007"),
      .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/300"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = base_version,
      .reason_code = std::string("temporary_debug"),
      .expires_at = std::string("2026-04-02T02:00:00Z"),
      .patches = {dasall::infra::config::ConfigPatchEntry{
          .op = dasall::infra::config::ConfigPatchOperation::Replace,
          .key_path = std::string("infra.config.validation.strict"),
          .value = dasall::infra::config::TypedConfig{
              .key_path = std::string("infra.config.validation.strict"),
              .value_type = dasall::infra::config::ConfigValueType::Boolean,
              .serialized_value = std::move(serialized_value),
              .schema_version = std::string("1"),
              .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/300"),
              .secret_backed = false,
          },
      }},
  };
}

void test_config_center_facade_rejects_uninitialized_and_invalid_bootstrap_paths() {
  using dasall::infra::config::ConfigCenterFacade;
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigRollbackToken;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigCenterFacade facade;

  const auto query_before_load = facade.get_typed(make_boolean_query("infra.config.validation.strict"));
  assert_true(!query_before_load.has_value(),
              "ConfigCenterFacade should keep get_typed empty before the bootstrap chain completes");

  const auto apply_before_load = facade.apply_override(make_runtime_override_patch(1, "false"));
  assert_true(!apply_before_load.applied,
              "ConfigCenterFacade should reject runtime overrides before an active snapshot exists");
  assert_true(apply_before_load.result_code == map_config_error_code(ConfigErrorCode::NotFound).result_code,
              "uninitialized apply_override should enter the frozen config error mapping path");

  const auto invalid_load = facade.load_layers(dasall::infra::config::ConfigStartupContext{
      .requested_profile_id = std::string("staging"),
      .deployment_source_ref = std::string(),
      .runtime_overlay_source_ref = std::string(),
      .actor_ref = std::string(),
      .load_runtime_overlay = false,
  });
  assert_true(!invalid_load.applied,
              "ConfigCenterFacade should reject unsupported profile aliases and missing actor attribution");
  assert_true(invalid_load.result_code ==
                  map_config_error_code(ConfigErrorCode::InvalidSchema).result_code &&
                  invalid_load.references_only_contract_error_types(),
              "invalid bootstrap requests should stay inside the frozen config error mapping path");

  const auto invalid_rollback = facade.rollback(ConfigRollbackToken{});
  assert_true(!invalid_rollback.applied,
              "ConfigCenterFacade should reject unspecified rollback tokens before any override is applied");
  assert_true(invalid_rollback.result_code ==
                  map_config_error_code(ConfigErrorCode::RollbackFailed).result_code,
              "rollback entry failures should stay inside the frozen config rollback mapping path");
}

void test_config_center_facade_loads_bootstrap_chain_and_restores_runtime_override() {
  using dasall::infra::config::ConfigCenterFacade;
  using dasall::infra::config::ConfigRollbackToken;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::tests::support::assert_true;

  ConfigCenterFacade facade;
  int notification_count = 0;

  const auto subscription = facade.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config.validation."),
      .subscriber_id = std::string("runtime-provider"),
      .callback = [&](const dasall::infra::config::ConfigDiff&) { ++notification_count; },
  });
  assert_true(subscription.has_value() && subscription->active,
              "ConfigCenterFacade should return an active subscription handle for namespace-filtered callbacks");

  const auto load_result = facade.load_layers(make_startup_context());
  assert_true(load_result.applied,
              "ConfigCenterFacade should complete the bootstrap load chain with the minimal startup context placeholder");

  const auto loaded_config = facade.get_typed(make_boolean_query("infra.config.validation.strict"));
  assert_true(loaded_config.has_value() && loaded_config->serialized_value == "true" &&
                  loaded_config->source_kind == ConfigSourceKind::Defaults,
              "ConfigCenterFacade should expose the bootstrap defaults entry after load_layers succeeds");

  const auto apply_result = facade.apply_override(make_runtime_override_patch(1, "false"));
  assert_true(apply_result.applied && !apply_result.rollback_token.empty(),
              "ConfigCenterFacade should accept a runtime override on the active snapshot and issue a rollback token");

  const auto overridden_config = facade.get_typed(make_boolean_query("infra.config.validation.strict"));
  assert_true(overridden_config.has_value() && overridden_config->serialized_value == "false" &&
                  overridden_config->source_kind == ConfigSourceKind::RuntimeOverride,
              "ConfigCenterFacade should expose the runtime override as the active typed config entry");
  assert_true(notification_count == 1,
              "ConfigCenterFacade should dispatch one namespace-matched notification for a successful runtime override");

  const auto rollback_result = facade.rollback(ConfigRollbackToken{
      .token = apply_result.rollback_token,
      .actor_ref = std::string("ops-user"),
  });
  assert_true(rollback_result.applied,
              "ConfigCenterFacade should restore the previous snapshot when given a matching rollback token");

  const auto rolled_back_config = facade.get_typed(make_boolean_query("infra.config.validation.strict"));
  assert_true(rolled_back_config.has_value() && rolled_back_config->serialized_value == "true" &&
                  rolled_back_config->source_kind == ConfigSourceKind::Defaults,
              "ConfigCenterFacade should restore the bootstrap value after rollback");
}

void test_config_center_facade_reports_conflicting_runtime_override_versions() {
  using dasall::infra::config::ConfigCenterFacade;
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigCenterFacade facade;
  const auto load_result = facade.load_layers(make_startup_context(false));
  assert_true(load_result.applied,
              "ConfigCenterFacade should support a bootstrap path without runtime overlay when loading is disabled");

  const auto conflict_result = facade.apply_override(make_runtime_override_patch(9, "false"));
  assert_true(!conflict_result.applied,
              "ConfigCenterFacade should reject runtime overrides whose base_version does not match the active snapshot");
  assert_true(conflict_result.result_code ==
                  map_config_error_code(ConfigErrorCode::Conflict).result_code &&
                  conflict_result.references_only_contract_error_types(),
              "stale runtime override requests should enter the frozen config conflict mapping path");
}

}  // namespace

int main() {
  try {
    test_config_center_facade_rejects_uninitialized_and_invalid_bootstrap_paths();
    test_config_center_facade_loads_bootstrap_chain_and_restores_runtime_override();
    test_config_center_facade_reports_conflicting_runtime_override_versions();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
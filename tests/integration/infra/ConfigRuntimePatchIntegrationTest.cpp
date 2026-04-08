#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config/ConfigCenterFacade.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::config::ConfigStartupContext make_startup_context() {
  return dasall::infra::config::ConfigStartupContext{
      .requested_profile_id = std::string("desktop_full"),
      .deployment_source_ref = std::string("deploy://site-001/config.yaml"),
      .runtime_overlay_source_ref = std::string("ops://window/bootstrap"),
      .actor_ref = std::string("runtime-bootstrap"),
      .load_runtime_overlay = true,
  };
}

dasall::infra::config::ConfigPatch make_runtime_override_patch() {
  return dasall::infra::config::ConfigPatch{
      .patch_id = std::string("runtime-patch-013"),
      .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
      .source_id = std::string("ops://ticket/301"),
      .actor = std::string("ops-user"),
      .target_scope = std::string("runtime"),
      .base_version = 1,
      .reason_code = std::string("temporary_debug"),
      .expires_at = std::string("2026-04-02T02:30:00Z"),
      .patches = {dasall::infra::config::ConfigPatchEntry{
          .op = dasall::infra::config::ConfigPatchOperation::Replace,
          .key_path = std::string("infra.config.validation.strict"),
          .value = dasall::infra::config::TypedConfig{
              .key_path = std::string("infra.config.validation.strict"),
              .value_type = dasall::infra::config::ConfigValueType::Boolean,
              .serialized_value = std::string("false"),
              .schema_version = std::string("1"),
              .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
              .source_id = std::string("ops://ticket/301"),
              .secret_backed = false,
          },
      }},
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

void test_runtime_patch_publish_integration_delivers_matching_subscribers() {
  using dasall::infra::config::ConfigCenterFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigCenterFacade facade;
  int matched_notification_count = 0;
  int unmatched_notification_count = 0;
  std::uint64_t observed_from_version = 0;
  std::uint64_t observed_to_version = 0;
  std::string observed_key_path;
  std::string observed_value;

  const auto matched_subscription = facade.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config.validation."),
      .subscriber_id = std::string("validation-monitor"),
      .callback = [&](const dasall::infra::config::ConfigDiff& diff) {
        ++matched_notification_count;
        observed_from_version = diff.from_version;
        observed_to_version = diff.to_version;
        observed_key_path = diff.changes.front().key_path;
        observed_value = diff.changes.front().to_serialized_value;
      },
  });
  const auto throwing_subscription = facade.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config."),
      .subscriber_id = std::string("throwing-monitor"),
      .callback = [](const dasall::infra::config::ConfigDiff&) {
        throw std::runtime_error("publisher should isolate integration subscriber failures");
      },
  });
  const auto unmatched_subscription = facade.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("runtime_budget."),
      .subscriber_id = std::string("budget-monitor"),
      .callback = [&](const dasall::infra::config::ConfigDiff&) { ++unmatched_notification_count; },
  });

  assert_true(matched_subscription.has_value() && matched_subscription->active,
              "ConfigCenterFacade should accept the matching runtime patch subscriber");
  assert_true(throwing_subscription.has_value() && throwing_subscription->active,
              "ConfigCenterFacade should accept the subscriber used to verify failure isolation");
  assert_true(unmatched_subscription.has_value() && unmatched_subscription->active,
              "ConfigCenterFacade should accept subscribers outside the current namespace");

  const auto load_result = facade.load_layers(make_startup_context());
  assert_true(load_result.applied,
              "ConfigCenterFacade should bootstrap successfully before runtime patch integration");

  const auto apply_result = facade.apply_override(make_runtime_override_patch());
  assert_true(apply_result.applied,
              "ConfigCenterFacade should keep apply_override successful when one subscriber callback throws");
  assert_true(!apply_result.rollback_token.empty(),
              "ConfigCenterFacade should still issue a rollback token on a published runtime override");

  assert_equal(1,
               matched_notification_count,
               "ConfigCenterFacade should publish one ConfigChanged event to the matched subscriber");
  assert_equal(0,
               unmatched_notification_count,
               "ConfigCenterFacade should not publish ConfigChanged events to unmatched subscribers");
  assert_true(observed_from_version == 1 && observed_to_version == 2,
              "ConfigCenterFacade should publish the runtime diff with ordered snapshot versions");
  assert_equal(std::string("infra.config.validation.strict"),
               observed_key_path,
               "ConfigCenterFacade should publish the changed key_path through the runtime patch diff");
  assert_equal(std::string("false"),
               observed_value,
               "ConfigCenterFacade should publish the overridden serialized value through the runtime patch diff");

  const auto overridden_config = facade.get_typed(make_boolean_query("infra.config.validation.strict"));
  assert_true(overridden_config.has_value() && overridden_config->serialized_value == "false",
              "ConfigCenterFacade should keep the runtime override active after the publish path completes");
}

}  // namespace

int main() {
  try {
    test_runtime_patch_publish_integration_delivers_matching_subscribers();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
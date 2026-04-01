#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "plugin/IPluginManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::plugin::PluginDescriptor make_descriptor(dasall::infra::plugin::PluginStatus status) {
  return dasall::infra::plugin::PluginDescriptor::normalize(dasall::infra::plugin::PluginDescriptor{
      .plugin_id = std::string("plugin.echo"),
      .version = std::string("1.0.0"),
      .abi = std::string("linux.gcc13"),
      .trust_level = dasall::infra::plugin::PluginTrustLevel::Internal,
      .status = status,
      .source = std::string("./plugins/plugin.echo"),
  });
}

dasall::infra::policy::PolicyDecisionRef make_policy_decision() {
  return dasall::infra::policy::PolicyDecisionRef{
      .decision = dasall::infra::policy::PolicyDecision::Allow,
      .reason_code = std::string("plugin_allowed"),
      .matched_rule_ids = {std::string("plugin-allowlist-rule")},
      .snapshot_id = std::string("policy-snapshot-001"),
      .generation = 4,
      .evidence_ref = std::string("audit:plugin-policy-allow-001"),
      .warnings = {},
  };
}

class NullPluginManager final : public dasall::infra::plugin::IPluginManager {
 public:
  [[nodiscard]] dasall::infra::plugin::PluginCatalog discover(
      std::string_view profile_id) const override {
    if (profile_id.empty()) {
      return dasall::infra::plugin::PluginCatalog{};
    }

    return dasall::infra::plugin::PluginCatalog{
        .discovered_plugins = {make_descriptor(dasall::infra::plugin::PluginStatus::Discovered)},
        .rejected_plugins = {},
    };
  }

  [[nodiscard]] dasall::infra::plugin::PluginValidationResult validate(
      const dasall::infra::plugin::PluginValidationRequest& request) const override {
    if (!request.is_valid()) {
      return dasall::infra::plugin::PluginValidationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          request.plugin_id,
          "plugin validation request must keep plugin_id/manifest_ref/package_ref/profile_id",
          "plugin.validate",
          "NullPluginManager",
          "plugin.validation.invalid-request");
    }

    return dasall::infra::plugin::PluginValidationResult::success(
        request.plugin_id,
        make_policy_decision(),
        std::string("signature-report:plugin.echo"),
        std::string("compat-report:plugin.echo"),
        std::string("observation:plugin.echo.validate"));
  }

  dasall::infra::plugin::PluginLoadResult load(
      std::string_view plugin_id,
      const dasall::infra::plugin::PluginLoadOptions& load_options) override {
    if (plugin_id.empty() || !load_options.is_valid()) {
      return dasall::infra::plugin::PluginLoadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string(plugin_id),
          dasall::infra::plugin::PluginOperationPhase::Load,
          "plugin load requires plugin_id and valid load options",
          "plugin.load",
          "NullPluginManager",
          "plugin.load.invalid-request");
    }

    return dasall::infra::plugin::PluginLoadResult::success(
        std::string(plugin_id),
        dasall::infra::plugin::PluginOperationPhase::Load,
        std::string("handle:plugin.echo"),
        std::string("audit:plugin.echo.load"));
  }

  dasall::infra::plugin::PluginUnloadResult unload(std::string_view plugin_id) override {
    if (plugin_id.empty()) {
      return dasall::infra::plugin::PluginUnloadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string(plugin_id),
          "plugin unload requires plugin_id",
          "plugin.unload",
          "NullPluginManager",
          "plugin.unload.invalid-request");
    }

    return dasall::infra::plugin::PluginUnloadResult::success(
        std::string(plugin_id),
        std::string("audit:plugin.echo.unload"));
  }

  [[nodiscard]] dasall::infra::plugin::ActivePluginSet list_active() const override {
    return dasall::infra::plugin::ActivePluginSet{
        .active_plugins = {make_descriptor(dasall::infra::plugin::PluginStatus::Active)},
        .safe_mode_active = false,
        .max_active = 16,
    };
  }
};

void test_plugin_manager_interface_freezes_manager_surface_and_boundary_types() {
  using dasall::infra::plugin::ActivePluginSet;
  using dasall::infra::plugin::IPluginManager;
  using dasall::infra::plugin::PluginCatalog;
  using dasall::infra::plugin::PluginLoadOptions;
  using dasall::infra::plugin::PluginLoadResult;
  using dasall::infra::plugin::PluginUnloadResult;
  using dasall::infra::plugin::PluginValidationRequest;
  using dasall::infra::plugin::PluginValidationResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<const IPluginManager&>().discover(
                                   std::declval<std::string_view>())),
                               PluginCatalog>);
  static_assert(std::is_same_v<decltype(std::declval<const IPluginManager&>().validate(
                                   std::declval<const PluginValidationRequest&>())),
                               PluginValidationResult>);
  static_assert(std::is_same_v<decltype(std::declval<IPluginManager&>().load(
                                   std::declval<std::string_view>(),
                                   std::declval<const PluginLoadOptions&>())),
                               PluginLoadResult>);
  static_assert(std::is_same_v<decltype(std::declval<IPluginManager&>().unload(
                                   std::declval<std::string_view>())),
                               PluginUnloadResult>);
  static_assert(std::is_same_v<decltype(std::declval<const IPluginManager&>().list_active()),
                               ActivePluginSet>);

  NullPluginManager manager;
  const auto catalog = manager.discover("desktop_full");
  assert_true(catalog.has_consistent_entries(),
              "IPluginManager discover should keep PluginCatalog as the frozen discovery boundary");

  const auto validation = manager.validate(dasall::infra::plugin::PluginValidationRequest{
      .plugin_id = std::string("plugin.echo"),
      .manifest_ref = std::string("manifest:plugin.echo@1"),
      .package_ref = std::string("package:plugin.echo@1"),
      .profile_id = std::string("desktop_full"),
  });
  assert_true(validation.accepted && validation.has_traceable_refs(),
              "IPluginManager validate should freeze a traceable boundary result with policy/signature/compat refs");

  const auto load = manager.load("plugin.echo",
                                 dasall::infra::plugin::PluginLoadOptions{
                                     .profile_id = std::string("desktop_full"),
                                     .actor_ref = std::string("runtime"),
                                     .timeout_ms = 3000,
                                     .audit_required = true,
                                     .dry_run = false,
                                 });
  assert_true(load.loaded && load.phase == dasall::infra::plugin::PluginOperationPhase::Load &&
                  !load.handle_ref.empty() && !load.evidence_ref.empty(),
              "IPluginManager load should freeze phase/handle_ref/evidence_ref as the load boundary");

  const auto unload = manager.unload("plugin.echo");
  assert_true(unload.unloaded && !unload.evidence_ref.empty(),
              "IPluginManager unload should freeze plugin_id plus evidence_ref as the unload boundary");

  const auto active = manager.list_active();
  assert_true(active.has_consistent_entries(),
              "IPluginManager list_active should freeze a unique active plugin set boundary");
}

void test_plugin_manager_interface_rejects_invalid_requests_with_contract_error_types() {
  using dasall::tests::support::assert_true;

  NullPluginManager manager;

  const auto invalid_validation = manager.validate(dasall::infra::plugin::PluginValidationRequest{});
  assert_true(!invalid_validation.accepted,
              "IPluginManager validate should reject default placeholder refs before downstream policy/signature checks");
  assert_true(invalid_validation.references_only_contract_error_types(),
              "validate failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto invalid_load = manager.load("", dasall::infra::plugin::PluginLoadOptions{});
  assert_true(!invalid_load.loaded,
              "IPluginManager load should reject empty plugin_id or unknown load options");
  assert_true(invalid_load.references_only_contract_error_types(),
              "load failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto invalid_unload = manager.unload("");
  assert_true(!invalid_unload.unloaded,
              "IPluginManager unload should reject empty plugin identifiers");
  assert_true(invalid_unload.references_only_contract_error_types(),
              "unload failures should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_plugin_manager_interface_freezes_manager_surface_and_boundary_types();
    test_plugin_manager_interface_rejects_invalid_requests_with_contract_error_types();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
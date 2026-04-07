#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "plugin/PluginAuditAdapter.h"
#include "plugin/PluginErrorCode.h"
#include "plugin/PluginLifecycleManager.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

[[nodiscard]] dasall::infra::plugin::PluginLoadOptions make_load_options() {
  return dasall::infra::plugin::PluginLoadOptions{
      .profile_id = std::string("desktop_full"),
      .actor_ref = std::string("runtime"),
      .timeout_ms = 3000,
      .audit_required = true,
      .dry_run = false,
  };
}

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);

    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.pop_front();
      return outcome;
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

void test_plugin_lifecycle_manager_transitions_loaded_plugin_to_active() {
  using dasall::infra::plugin::PluginLifecycleManager;
  using dasall::infra::plugin::PluginOperationPhase;
  using dasall::infra::plugin::PluginStatus;
  using dasall::tests::support::assert_true;

  PluginLifecycleManager manager;

  const auto load_result = manager.load("plugin.echo", make_load_options());
  assert_true(load_result.loaded &&
                  load_result.phase == PluginOperationPhase::Load &&
                  !load_result.handle_ref.empty() && !load_result.evidence_ref.empty(),
              "PluginLifecycleManager should surface a traceable load boundary when the runtime load succeeds");
  assert_true(manager.current_status("plugin.echo").has_value() &&
                  *manager.current_status("plugin.echo") == PluginStatus::Loaded,
              "PluginLifecycleManager should keep successfully loaded plugins in the Loaded state before activation");

  const auto enable_result = manager.enable("plugin.echo");
  assert_true(enable_result.transitioned &&
                  enable_result.from_status == PluginStatus::Loaded &&
                  enable_result.to_status == PluginStatus::Active &&
                  enable_result.references_only_contract_error_types(),
              "PluginLifecycleManager should allow a Loaded plugin to transition into Active");
  assert_true(manager.current_status("plugin.echo").has_value() &&
                  *manager.current_status("plugin.echo") == PluginStatus::Active,
              "PluginLifecycleManager should persist the Active state after enable succeeds");
  assert_true(manager.list_active().has_consistent_entries(),
              "PluginLifecycleManager should keep ActivePluginSet consistent after activation");
}

void test_plugin_lifecycle_manager_transitions_loaded_plugin_to_disabled_and_unloaded() {
  using dasall::infra::plugin::PluginLifecycleManager;
  using dasall::infra::plugin::PluginStatus;
  using dasall::tests::support::assert_true;

  PluginLifecycleManager manager;

  const auto load_result = manager.load("plugin.echo", make_load_options());
  assert_true(load_result.loaded,
              "PluginLifecycleManager should be able to enter the Loaded state before disable/unload tests");

  const auto disable_result = manager.disable("plugin.echo");
  assert_true(disable_result.transitioned &&
                  disable_result.from_status == PluginStatus::Loaded &&
                  disable_result.to_status == PluginStatus::Disabled,
              "PluginLifecycleManager should allow a Loaded plugin to transition into Disabled");
  assert_true(manager.current_status("plugin.echo").has_value() &&
                  *manager.current_status("plugin.echo") == PluginStatus::Disabled,
              "PluginLifecycleManager should persist the Disabled state after disable succeeds");

  const auto unload_result = manager.unload("plugin.echo");
  assert_true(unload_result.unloaded && unload_result.references_only_contract_error_types(),
              "PluginLifecycleManager should unload a Disabled plugin and keep the frozen unload boundary");
  assert_true(!manager.current_status("plugin.echo").has_value() &&
                  manager.list_active().active_plugins.empty(),
              "PluginLifecycleManager should remove unloaded plugins from the active set");
}

void test_plugin_lifecycle_manager_cleans_up_failed_loads_and_triggers_safe_mode() {
  using dasall::infra::plugin::PluginErrorCode;
  using dasall::infra::plugin::PluginLifecycleManager;
  using dasall::infra::plugin::PluginRuntimeLoadResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  int runtime_load_calls = 0;
  PluginLifecycleManager manager(
      [&runtime_load_calls](std::string_view plugin_id,
                            const dasall::infra::plugin::PluginLoadOptions&) {
        ++runtime_load_calls;
        return PluginRuntimeLoadResult::failure(
            dasall::infra::plugin::map_plugin_error_code(PluginErrorCode::LoadFail)
                .result_code,
            std::string("plugin_load_runtime_failed"),
            std::string("mock://plugin/load/") +
                dasall::infra::plugin::plugin_value_or_unknown(plugin_id),
            std::string("mock runtime bridge rejected plugin load"));
      },
      {},
      nullptr,
      16,
      2);

  const auto first_failure = manager.load("plugin.echo", make_load_options());
  assert_true(!first_failure.loaded && first_failure.references_only_contract_error_types(),
              "PluginLifecycleManager should surface runtime load failures through the frozen load boundary");
  assert_true(!manager.current_status("plugin.echo").has_value() &&
                  manager.list_active().active_plugins.empty() &&
                  manager.consecutive_failures() == 1 && !manager.safe_mode_active(),
              "PluginLifecycleManager should clean up failed loads without leaving ghost entries behind");

  const auto second_failure = manager.load("plugin.echo", make_load_options());
  assert_true(!second_failure.loaded && manager.safe_mode_active() &&
                  manager.consecutive_failures() == 2,
              "PluginLifecycleManager should trigger safe_mode after the configured consecutive failure threshold");

  const auto safe_mode_failure = manager.load("plugin.echo", make_load_options());
  assert_true(!safe_mode_failure.loaded &&
                  safe_mode_failure.evidence_ref ==
                      "plugin.load.plugin.echo.safe-mode",
              "PluginLifecycleManager should stop calling the runtime load bridge once safe_mode is active");
  assert_equal(2,
               runtime_load_calls,
               "PluginLifecycleManager should not invoke the runtime load bridge after safe_mode blocks new loads");
}

void test_plugin_lifecycle_manager_audits_failed_unloads_when_adapter_is_available() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::plugin::PluginAuditAdapter;
  using dasall::infra::plugin::PluginLifecycleManager;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  PluginAuditAdapter audit_adapter(logger);
  PluginLifecycleManager manager({}, {}, &audit_adapter);

  const auto unload_result = manager.unload("plugin.echo");
  assert_true(!unload_result.unloaded && unload_result.references_only_contract_error_types(),
              "PluginLifecycleManager should reject unload requests for plugins that are not currently managed");
  assert_equal(1,
               static_cast<int>(logger->events.size()),
               "PluginLifecycleManager should emit one plugin.unload audit event for a failed unload path when an adapter is present");

  const auto& event = logger->events.front();
  const auto& context = logger->contexts.front();
  assert_true(event.action == "plugin.unload" &&
                  event.outcome == AuditOutcome::Failed &&
                  event.evidence_ref.ref == unload_result.evidence_ref &&
                  has_side_effect(event, "reason_code:plugin_unload_not_loaded"),
              "PluginLifecycleManager should audit failed unloads with a stable action, failure outcome, and reason_code");
  assert_true(context.worker_type == "plugin",
              "PluginLifecycleManager should preserve the plugin worker_type when emitting lifecycle audit events");
}

}  // namespace

int main() {
  try {
    test_plugin_lifecycle_manager_transitions_loaded_plugin_to_active();
    test_plugin_lifecycle_manager_transitions_loaded_plugin_to_disabled_and_unloaded();
    test_plugin_lifecycle_manager_cleans_up_failed_loads_and_triggers_safe_mode();
    test_plugin_lifecycle_manager_audits_failed_unloads_when_adapter_is_available();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
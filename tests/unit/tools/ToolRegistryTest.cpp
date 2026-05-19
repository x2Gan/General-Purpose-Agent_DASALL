#include <exception>
#include <iostream>
#include <vector>

#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(
    const std::string& tool_name,
    const std::string& display_name) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = tool_name,
      .display_name = display_name,
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 5000U,
      .input_schema_ref = std::string("schema://tools/") + tool_name + "/input/v1",
      .output_schema_ref = std::string("schema://tools/") + tool_name + "/output/v1",
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::mcp::MCPToolBinding make_binding(
    const std::string& internal_tool_name,
    const std::string& remote_tool_name,
    const std::string& server_id) {
  return dasall::tools::mcp::MCPToolBinding{
      .internal_tool_name = internal_tool_name,
      .remote_tool_name = remote_tool_name,
      .server_id = server_id,
      .remote_capability_id = std::string("capability://") + server_id + "/" + remote_tool_name,
      .input_schema_ref = std::string("schema://mcp/") + remote_tool_name + "/input/v1",
  };
}

void test_register_builtin_updates_descriptor_view() {
  using dasall::tools::registry::ToolRegistry;

  ToolRegistry registry(std::vector<dasall::contracts::ToolDescriptor>{});
  auto descriptor = make_descriptor("agent.echo", "Echo");

  assert_true(registry.register_builtin(descriptor),
              "register_builtin should accept a valid builtin descriptor");

  const auto resolved_once = registry.resolve_descriptor("agent.echo");
  assert_true(resolved_once.has_value(),
              "resolve_descriptor should find the registered builtin descriptor");
  assert_equal(std::string("Echo"), resolved_once->display_name.value_or(""),
               "resolve_descriptor should expose the stored display_name");

  descriptor.display_name = std::string("Echo v2");
  descriptor.tags = std::vector<std::string>{"builtin", "echo"};

  assert_true(registry.register_builtin(descriptor),
              "register_builtin should replace an existing builtin descriptor by tool_name");

  const auto resolved_twice = registry.resolve_descriptor("agent.echo");
  assert_true(resolved_twice.has_value(),
              "resolve_descriptor should keep returning the updated builtin descriptor");
  assert_equal(std::string("Echo v2"), resolved_twice->display_name.value_or(""),
               "re-registering a builtin should update the descriptor payload");

  const auto descriptors = registry.list_descriptors();
  assert_equal(1, static_cast<int>(descriptors.size()),
               "list_descriptors should expose one builtin after replacement");

  assert_true(!registry.revoke_source(dasall::tools::registry::kBuiltinSourceKey),
              "builtin source should be protected from source-scoped revoke");
  assert_true(registry.resolve_descriptor("agent.echo").has_value(),
              "protected builtin revoke must not delete builtin descriptors");
}

void test_invalid_inputs_fail_closed() {
  using dasall::tools::registry::ToolRegistry;

  ToolRegistry registry(std::vector<dasall::contracts::ToolDescriptor>{});
  auto invalid_descriptor = make_descriptor("agent.invalid", "Invalid");
  invalid_descriptor.category.reset();

  assert_true(!registry.register_builtin(invalid_descriptor),
              "register_builtin should reject descriptors that fail contract validation");
  assert_equal(0, static_cast<int>(registry.list_descriptors().size()),
               "invalid builtin descriptors must not partially mutate the registry");

  const auto unknown_binding = make_binding("agent.missing", "missing.remote", "loopback");
  assert_true(!registry.upsert_mcp_bindings("plugin:loopback", {unknown_binding}),
              "upsert_mcp_bindings should reject bindings whose descriptor is absent");

  assert_true(registry.register_builtin(make_descriptor("agent.conflict", "Builtin Conflict")),
              "collision test requires a pre-existing descriptor owned by another source");
  assert_true(!registry.apply_plugin_extension_delta(
                  "plugin:loopback",
                  {make_descriptor("agent.conflict", "Plugin Conflict")} ),
              "apply_plugin_extension_delta should reject plugin descriptors that collide with existing tool names");
}

void test_mcp_binding_reconcile_and_source_revoke() {
  using dasall::tools::registry::ToolRegistry;

  ToolRegistry registry(std::vector<dasall::contracts::ToolDescriptor>{});
  assert_true(registry.register_builtin(make_descriptor("agent.echo", "Echo")),
              "precondition: builtin descriptor should exist before mcp bindings are attached");

  const auto primary_binding = make_binding("agent.echo", "echo.remote.v1", "loopback");
  const auto updated_binding = make_binding("agent.echo", "echo.remote.v2", "loopback");
  const auto backup_binding = make_binding("agent.echo", "echo.remote.backup", "backup");

  assert_true(registry.upsert_mcp_bindings("plugin:loopback", {primary_binding}),
              "upsert_mcp_bindings should register the first source-owned binding batch");

  auto bindings = registry.list_mcp_bindings("agent.echo");
  assert_equal(1, static_cast<int>(bindings.size()),
               "list_mcp_bindings should surface the source-owned binding");
  assert_equal(std::string("echo.remote.v1"), bindings.front().remote_tool_name,
               "binding view should expose the original remote tool name");

  assert_true(registry.upsert_mcp_bindings("plugin:loopback", {updated_binding}),
              "upsert_mcp_bindings should reconcile a source to the latest binding batch");

  bindings = registry.list_mcp_bindings("agent.echo");
  assert_equal(1, static_cast<int>(bindings.size()),
               "source reconcile should replace stale bindings instead of duplicating them");
  assert_equal(std::string("echo.remote.v2"), bindings.front().remote_tool_name,
               "source reconcile should expose the updated remote tool name");

  assert_true(registry.upsert_mcp_bindings("plugin:backup", {backup_binding}),
              "a second source should be able to publish an independent binding batch");

  bindings = registry.list_mcp_bindings("agent.echo");
  assert_equal(2, static_cast<int>(bindings.size()),
               "binding view should aggregate batches from independent sources");

  assert_true(registry.revoke_source("plugin:loopback"),
              "revoke_source should delete only the targeted source-owned bindings");

  bindings = registry.list_mcp_bindings("agent.echo");
  assert_equal(1, static_cast<int>(bindings.size()),
               "source revoke should leave unrelated binding sources intact");
  assert_equal(std::string("backup"), bindings.front().server_id,
               "remaining binding should belong to the untouched source");
}

void test_plugin_descriptor_delta_and_binding_revoke_are_isolated() {
  using dasall::tools::registry::ToolRegistry;

  ToolRegistry registry(std::vector<dasall::contracts::ToolDescriptor>{});
  const auto plugin_descriptor = make_descriptor("plugin.echo", "Plugin Echo");
  const auto initial_binding = make_binding("plugin.echo", "echo.remote.v1", "loopback");
  const auto updated_binding = make_binding("plugin.echo", "echo.remote.v2", "loopback");

  assert_true(registry.apply_plugin_extension_delta("plugin:loopback", {plugin_descriptor}),
              "plugin descriptor delta should publish source-owned descriptors into the registry");
  assert_true(registry.resolve_descriptor("plugin.echo").has_value(),
              "plugin descriptor delta should make the source-owned descriptor resolvable");

  assert_true(registry.upsert_mcp_bindings("plugin:loopback", {initial_binding}),
              "source-owned MCP bindings should attach to plugin-delivered descriptors");
  assert_equal(1, static_cast<int>(registry.list_mcp_bindings("plugin.echo").size()),
               "plugin-delivered descriptors should accept matching source-owned MCP bindings");

  assert_true(registry.revoke_mcp_bindings_for_source("plugin:loopback"),
              "binding-only revoke should drop source-owned MCP bindings without deleting descriptors");
  assert_equal(0, static_cast<int>(registry.list_mcp_bindings("plugin.echo").size()),
               "binding-only revoke should remove the source-owned MCP bindings");
  assert_true(registry.resolve_descriptor("plugin.echo").has_value(),
              "binding-only revoke must preserve plugin-delivered descriptors for the same source");

  assert_true(registry.upsert_mcp_bindings("plugin:loopback", {updated_binding}),
              "source-owned bindings should remain re-publishable after a binding-only revoke");
  assert_true(registry.resolve_descriptor("plugin.echo").has_value(),
              "re-publishing MCP bindings must not disturb the source-owned descriptor");

  assert_true(registry.revoke_source("plugin:loopback"),
              "full source revoke should delete both plugin-delivered descriptors and MCP bindings");
  assert_true(!registry.resolve_descriptor("plugin.echo").has_value(),
              "full source revoke should remove the plugin-delivered descriptor");
  assert_equal(0, static_cast<int>(registry.list_mcp_bindings("plugin.echo").size()),
               "full source revoke should remove the source-owned MCP bindings");
}

}  // namespace

int main() {
  try {
    test_register_builtin_updates_descriptor_view();
    test_invalid_inputs_fail_closed();
    test_mcp_binding_reconcile_and_source_revoke();
    test_plugin_descriptor_delta_and_binding_revoke_are_isolated();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
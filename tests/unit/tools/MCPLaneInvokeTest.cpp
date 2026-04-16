#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mcp/MCPLane.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class RecordingAdapter final : public dasall::tools::mcp::IMCPAdapter {
 public:
  dasall::tools::mcp::MCPServerSession session_to_return{
      .server_id = "mcp.echo",
      .transport_kind = dasall::tools::mcp::MCPTransportKind::stdio,
      .session_ref = "mcp.echo:session:conn-1",
      .negotiated_protocol_version = std::string("2025-03-26"),
      .transport_connection_id = std::string("conn-1"),
  };
  dasall::contracts::ToolResult result_to_return{
      .request_id = std::string("req-1"),
      .tool_call_id = std::string("call-1"),
      .tool_name = std::string("tool.echo"),
      .success = true,
      .payload = std::string("ok"),
      .error = std::nullopt,
      .side_effects = std::nullopt,
      .completed_at = 1000,
      .duration_ms = 1,
      .goal_id = std::string("goal-1"),
      .worker_task_id = std::string("worker-1"),
      .tags = std::nullopt,
  };
  std::optional<dasall::tools::mcp::MCPServerSpec> last_spec;
  std::optional<dasall::tools::mcp::MCPToolBinding> last_binding;
  std::optional<dasall::contracts::ToolIR> last_tool_ir;

  [[nodiscard]] dasall::tools::mcp::MCPServerSession ensure_session(
      const dasall::tools::mcp::MCPServerSpec& spec) override {
    last_spec = spec;
    return session_to_return;
  }

  [[nodiscard]] dasall::tools::CapabilitySnapshot list_capabilities(
      const dasall::tools::mcp::MCPServerSession& session) override {
    return dasall::tools::CapabilitySnapshot{
        .server_id = session.server_id,
        .entries = {},
        .freshness = dasall::tools::CapabilityFreshness::fresh,
        .last_refresh_at_ms = 1000,
        .last_error = std::nullopt,
        .trust_marker = std::string("trusted"),
    };
  }

  [[nodiscard]] dasall::contracts::ToolResult invoke(
      const dasall::tools::mcp::MCPServerSession& session,
      const dasall::tools::mcp::MCPToolBinding& binding,
      const dasall::contracts::ToolIR& tool_ir) override {
    static_cast<void>(session);
    last_binding = binding;
    last_tool_ir = tool_ir;
    return result_to_return;
  }
};

[[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("tool.echo"),
      .display_name = std::string("Echo"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 2500U,
      .input_schema_ref = std::string("schema://input"),
      .output_schema_ref = std::string("schema://output"),
      .required_scopes = std::vector<std::string>{"trusted"},
      .tags = std::vector<std::string>{"tool.route.mcp"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::tools::mcp::MCPToolBinding make_binding() {
  return dasall::tools::mcp::MCPToolBinding{
      .internal_tool_name = "tool.echo",
      .remote_tool_name = "remote.echo",
      .server_id = "mcp.echo",
      .remote_capability_id = std::string("cap.echo"),
      .input_schema_ref = std::string("schema://input"),
  };
}

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-1"),
      .tool_call_id = std::string("call-1"),
      .tool_name = std::string("tool.echo"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{}"),
      .route = dasall::contracts::ToolIRRoute::MCPRemote,
      .timeout_ms = 2000U,
      .idempotency_key = std::string("idem-1"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-1"),
      .worker_task_id = std::string("worker-1"),
  };
}

void test_mcp_lane_resolves_binding_and_executes_with_adapter() {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{make_descriptor()});
  const auto binding = make_binding();
  assert_true(
      registry->upsert_mcp_bindings("plugin.echo", {binding}),
      "registry should accept a binding batch when the internal tool descriptor exists");

  auto adapter = std::make_shared<RecordingAdapter>();
  dasall::tools::mcp::MCPLane lane{
      dasall::tools::mcp::MCPLaneDependencies{
          .registry = registry,
          .adapter = adapter,
          .server_spec_resolver = [](std::string_view server_id)
              -> std::optional<dasall::tools::mcp::MCPServerSpec> {
            return dasall::tools::mcp::MCPServerSpec{
                .server_id = std::string(server_id),
                .transport_kind = dasall::tools::mcp::MCPTransportKind::stdio,
                .endpoint_ref = "loopback://mcp.echo",
                .declared_capabilities = {"tools"},
                .trust_level = "trusted",
                .healthcheck_ref = std::nullopt,
            };
          },
      }};

  const auto result = lane.execute(
      make_tool_ir(),
      dasall::tools::ToolExecutionContext{
          .invocation_context = {},
          .lane_key = std::string("mcp:mcp.echo"),
      },
      std::string("mcp.echo"));

  assert_true(result.success.value_or(false),
              "mcp lane should forward successful invoke results from the adapter");
  assert_equal(std::string("remote.echo"), adapter->last_binding->remote_tool_name,
               "mcp lane should resolve the remote binding before invoke");
  assert_equal(std::string("loopback://mcp.echo"), adapter->last_spec->endpoint_ref,
               "mcp lane should resolve the server spec before ensuring the session");
  assert_equal(std::string("tool.echo"), adapter->last_tool_ir->tool_name.value_or(""),
               "mcp lane should forward the validated tool ir to the adapter");
}

void test_mcp_lane_fails_closed_when_binding_is_missing() {
  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
      std::vector<dasall::contracts::ToolDescriptor>{make_descriptor()});
  auto adapter = std::make_shared<RecordingAdapter>();
  dasall::tools::mcp::MCPLane lane{
      dasall::tools::mcp::MCPLaneDependencies{
          .registry = registry,
          .adapter = adapter,
          .server_spec_resolver = {},
      }};

  const auto result = lane.execute(
      make_tool_ir(),
      dasall::tools::ToolExecutionContext{
          .invocation_context = {},
          .lane_key = std::string("mcp:mcp.echo"),
      },
      std::string("mcp.echo"));

  assert_true(!result.success.value_or(true),
              "mcp lane should fail closed when no binding is available");
  assert_equal(std::string("mcp.binding_missing"), result.error->details.message,
               "missing binding should return a stable failure reason");
}

}  // namespace

int main() {
  try {
    test_mcp_lane_resolves_binding_and_executes_with_adapter();
    test_mcp_lane_fails_closed_when_binding_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
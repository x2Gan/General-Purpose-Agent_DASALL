#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "error/ErrorInfo.h"
#include "mcp/IMCPAdapter.h"
#include "mcp/IMCPTransport.h"

namespace {

using dasall::contracts::ErrorInfo;
using dasall::tools::CapabilityFreshness;
using dasall::tools::CapabilitySnapshot;
using dasall::tools::mcp::IMCPAdapter;
using dasall::tools::mcp::IMCPTransport;
using dasall::tools::mcp::MCPServerSession;
using dasall::tools::mcp::MCPServerSpec;
using dasall::tools::mcp::MCPToolBinding;
using dasall::tools::mcp::MCPTransportKind;
using dasall::tools::mcp::TransportConnectResult;

static_assert(std::is_same_v<decltype(&IMCPTransport::connect),
                             TransportConnectResult (IMCPTransport::*)(
                                 const MCPServerSpec&)>);
static_assert(std::is_same_v<decltype(&IMCPTransport::send),
                             void (IMCPTransport::*)(std::string_view)>);
static_assert(std::is_same_v<decltype(&IMCPTransport::receive),
                             std::optional<std::string> (IMCPTransport::*)(
                                 std::chrono::milliseconds)>);
static_assert(std::is_same_v<decltype(&IMCPTransport::close),
                             void (IMCPTransport::*)()>);
static_assert(std::is_same_v<decltype(&IMCPTransport::is_connected),
                             bool (IMCPTransport::*)() const>);
static_assert(std::is_abstract_v<IMCPTransport>);

static_assert(std::is_same_v<decltype(&IMCPAdapter::ensure_session),
                             MCPServerSession (IMCPAdapter::*)(
                                 const MCPServerSpec&)>);
static_assert(std::is_same_v<decltype(&IMCPAdapter::list_capabilities),
                             CapabilitySnapshot (IMCPAdapter::*)(
                                 const MCPServerSession&)>);
static_assert(std::is_same_v<decltype(&IMCPAdapter::invoke),
                             dasall::contracts::ToolResult (IMCPAdapter::*)(
                                 const MCPServerSession&,
                                 const MCPToolBinding&,
                                 const dasall::contracts::ToolIR&)>);
static_assert(std::is_abstract_v<IMCPAdapter>);

void mcp_surfaces_keep_transport_raw_and_adapter_protocol_facing_shapes() {
  const MCPServerSpec spec{
    .server_id = std::string("mcp://local-shell"),
    .transport_kind = MCPTransportKind::stdio,
    .endpoint_ref = std::string("plugin:local-shell"),
    .declared_capabilities = {"tools", "resources"},
    .trust_level = std::string("trusted-local"),
    .healthcheck_ref = std::string("stdio://ping"),
  };

  const TransportConnectResult connect_result{
    .connected = true,
    .connection_id = std::string("transport-1"),
    .error = std::nullopt,
  };

  const MCPServerSession session{
    .server_id = std::string("mcp://local-shell"),
    .transport_kind = MCPTransportKind::stdio,
    .session_ref = std::string("session-1"),
    .negotiated_protocol_version = std::string("2025-03-26"),
    .transport_connection_id = std::string("transport-1"),
  };

  const CapabilitySnapshot snapshot{
    .server_id = std::string("mcp://local-shell"),
    .entries = {},
    .freshness = CapabilityFreshness::fresh,
    .last_refresh_at_ms = 1713139200000,
    .last_error = std::nullopt,
    .trust_marker = std::string("trusted-local"),
  };

  const MCPToolBinding binding{
    .internal_tool_name = std::string("agent.shell"),
    .remote_tool_name = std::string("exec"),
    .server_id = std::string("mcp://local-shell"),
    .remote_capability_id = std::string("tool.exec"),
    .input_schema_ref = std::string("schema://exec-input"),
  };

  const ErrorInfo protocol_error{
    .failure_type = dasall::contracts::ResultCodeCategory::Provider,
    .retryable = false,
    .safe_to_replan = true,
    .details = {
      .code = 4001,
      .message = "transport closed unexpectedly",
      .stage = "mcp.transport",
    },
    .source_ref = {
      .ref_type = "mcp_server",
      .ref_id = "mcp://local-shell",
    },
  };

  static_cast<void>(spec);
  static_cast<void>(connect_result);
  static_cast<void>(session);
  static_cast<void>(snapshot);
  static_cast<void>(binding);
  static_cast<void>(protocol_error);
}

}  // namespace
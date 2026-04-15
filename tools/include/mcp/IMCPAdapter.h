#pragma once

#include <optional>
#include <string>

#include "ICapabilityCache.h"
#include "mcp/IMCPTransport.h"
#include "tool/ToolIR.h"
#include "tool/ToolResult.h"

namespace dasall::tools::mcp {

struct MCPServerSession {
	std::string server_id;
	MCPTransportKind transport_kind = MCPTransportKind::stdio;
	std::string session_ref;
	std::optional<std::string> negotiated_protocol_version;
	std::optional<std::string> transport_connection_id;

	[[nodiscard]] bool ready() const {
		return !session_ref.empty();
	}
};

struct MCPToolBinding {
	std::string internal_tool_name;
	std::string remote_tool_name;
	std::string server_id;
	std::optional<std::string> remote_capability_id;
	std::optional<std::string> input_schema_ref;
};

class IMCPAdapter {
 public:
	virtual ~IMCPAdapter() = default;

	virtual MCPServerSession ensure_session(const MCPServerSpec& spec) = 0;

	virtual dasall::tools::CapabilitySnapshot list_capabilities(
			const MCPServerSession& session) = 0;

	virtual dasall::contracts::ToolResult invoke(
			const MCPServerSession& session,
			const MCPToolBinding& binding,
			const dasall::contracts::ToolIR& tool_ir) = 0;
};

}  // namespace dasall::tools::mcp
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error/ErrorInfo.h"

namespace dasall::tools::mcp {

enum class MCPTransportKind {
	stdio,
	sse,
	streamable_http,
};

struct MCPServerSpec {
	std::string server_id;
	MCPTransportKind transport_kind = MCPTransportKind::stdio;
	std::string endpoint_ref;
	std::vector<std::string> declared_capabilities;
	std::string trust_level;
	std::optional<std::string> healthcheck_ref;
};

struct TransportConnectResult {
	bool connected = false;
	std::optional<std::string> connection_id;
	std::optional<dasall::contracts::ErrorInfo> error;

	[[nodiscard]] bool ok() const {
		return connected && connection_id.has_value();
	}
};

class IMCPTransport {
 public:
	virtual ~IMCPTransport() = default;

	virtual TransportConnectResult connect(const MCPServerSpec& spec) = 0;

	virtual void send(std::string_view json_rpc_message) = 0;

	[[nodiscard]] virtual std::optional<std::string> receive(
			std::chrono::milliseconds timeout) = 0;

	virtual void close() = 0;

	[[nodiscard]] virtual bool is_connected() const = 0;
};

}  // namespace dasall::tools::mcp
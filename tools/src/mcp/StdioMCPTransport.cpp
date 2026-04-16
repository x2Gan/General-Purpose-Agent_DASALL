#include "mcp/StdioMCPTransport.h"

#include <stdexcept>
#include <utility>

#include "error/ResultCode.h"

namespace {

[[nodiscard]] dasall::contracts::ErrorInfo build_error(
    dasall::contracts::ResultCode code,
    std::string message,
    std::string stage,
    std::string ref_id) {
  return dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::classify_result_code(code),
      .retryable = code == dasall::contracts::ResultCode::ProviderTimeout,
      .safe_to_replan = true,
      .details = dasall::contracts::ErrorDetails{
          .code = static_cast<int>(code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = "mcp.stdio_transport",
          .ref_id = std::move(ref_id),
      },
  };
}

}  // namespace

namespace dasall::tools::mcp {

StdioMCPTransport::StdioMCPTransport(StdioTransportChannelFactory channel_factory)
    : channel_factory_(std::move(channel_factory)) {}

TransportConnectResult StdioMCPTransport::connect(const MCPServerSpec& spec) {
  close();

  if (spec.transport_kind != MCPTransportKind::stdio) {
    return TransportConnectResult{
        .connected = false,
        .connection_id = std::nullopt,
        .error = build_error(
            contracts::ResultCode::ToolExecutionFailed,
            "mcp.stdio.unsupported_transport_kind",
            "tools.mcp.stdio.connect",
            spec.server_id),
    };
  }

  if (!channel_factory_) {
    return TransportConnectResult{
        .connected = false,
        .connection_id = std::nullopt,
        .error = build_error(
            contracts::ResultCode::ToolExecutionFailed,
            "mcp.stdio.channel_factory_unconfigured",
            "tools.mcp.stdio.connect",
            spec.server_id),
    };
  }

  channel_ = channel_factory_(spec);
  if (!channel_) {
    return TransportConnectResult{
        .connected = false,
        .connection_id = std::nullopt,
        .error = build_error(
            contracts::ResultCode::ToolExecutionFailed,
            "mcp.stdio.channel_unavailable",
            "tools.mcp.stdio.connect",
            spec.server_id),
    };
  }

  const auto result = channel_->open(spec);
  if (!result.ok()) {
    channel_.reset();
    return result;
  }

  return result;
}

void StdioMCPTransport::send(std::string_view json_rpc_message) {
  if (!is_connected()) {
    throw std::runtime_error("mcp.stdio.not_connected");
  }

  channel_->write(json_rpc_message);
}

std::optional<std::string> StdioMCPTransport::receive(std::chrono::milliseconds timeout) {
  if (!is_connected()) {
    return std::nullopt;
  }

  return channel_->read(timeout);
}

void StdioMCPTransport::close() {
  if (channel_) {
    channel_->close();
    channel_.reset();
  }
}

bool StdioMCPTransport::is_connected() const {
  return channel_ && channel_->connected();
}

}  // namespace dasall::tools::mcp
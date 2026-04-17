#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>

#include "mcp/IMCPAdapter.h"

namespace dasall::tools::mcp {

using MCPTransportFactory = std::function<std::shared_ptr<IMCPTransport>(MCPTransportKind kind)>;

struct MCPAdapterDependencies {
  MCPTransportFactory transport_factory;
  std::function<std::int64_t()> now_ms;
  std::function<std::string(std::string_view server_id, std::string_view connection_id)>
      session_ref_builder;
  std::chrono::milliseconds request_timeout = std::chrono::milliseconds(250);
};

class MCPAdapter final : public IMCPAdapter {
 public:
  MCPAdapter();
  explicit MCPAdapter(MCPAdapterDependencies dependencies);

  [[nodiscard]] MCPServerSession ensure_session(const MCPServerSpec& spec) override;
  [[nodiscard]] CapabilitySnapshot list_capabilities(
      const MCPServerSession& session) override;
  [[nodiscard]] dasall::contracts::ToolResult invoke(
      const MCPServerSession& session,
      const MCPToolBinding& binding,
      const dasall::contracts::ToolIR& tool_ir) override;

 private:
  struct SessionRecord {
    MCPServerSpec spec;
    MCPServerSession session;
    std::shared_ptr<IMCPTransport> transport;
  };

  [[nodiscard]] static MCPAdapterDependencies default_dependencies();
  [[nodiscard]] std::shared_ptr<IMCPTransport> select_transport(
      MCPTransportKind kind) const;
  [[nodiscard]] std::int64_t current_time_ms() const;
  [[nodiscard]] std::string build_session_ref(
      std::string_view server_id,
      std::string_view connection_id) const;
  [[nodiscard]] MCPServerSession perform_handshake(
      const MCPServerSpec& spec,
      const TransportConnectResult& connect_result,
      const std::shared_ptr<IMCPTransport>& transport) const;
  [[nodiscard]] std::optional<SessionRecord> resolve_session_record(
      const MCPServerSession& session) const;
  [[nodiscard]] dasall::contracts::ToolResult map_protocol_error(
      const MCPServerSession& session,
      const dasall::contracts::ToolIR& tool_ir,
      std::string reason_code,
      std::string message,
      dasall::contracts::ResultCode result_code) const;

  MCPAdapterDependencies dependencies_;
  mutable std::shared_mutex sessions_mutex_;
  std::map<std::string, SessionRecord> sessions_by_server_;
};

}  // namespace dasall::tools::mcp
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "mcp/IMCPTransport.h"

namespace dasall::tools::mcp {

class IStdioTransportChannel {
 public:
  virtual ~IStdioTransportChannel() = default;

  [[nodiscard]] virtual TransportConnectResult open(const MCPServerSpec& spec) = 0;
  virtual void write(std::string_view json_rpc_message) = 0;
  [[nodiscard]] virtual std::optional<std::string> read(
      std::chrono::milliseconds timeout) = 0;
  virtual void close() = 0;
  [[nodiscard]] virtual bool connected() const = 0;
};

using StdioTransportChannelFactory =
    std::function<std::unique_ptr<IStdioTransportChannel>(const MCPServerSpec& spec)>;

class StdioMCPTransport final : public IMCPTransport {
 public:
  explicit StdioMCPTransport(StdioTransportChannelFactory channel_factory = {});

  [[nodiscard]] TransportConnectResult connect(const MCPServerSpec& spec) override;
  void send(std::string_view json_rpc_message) override;
  [[nodiscard]] std::optional<std::string> receive(
      std::chrono::milliseconds timeout) override;
  void close() override;
  [[nodiscard]] bool is_connected() const override;

 private:
  StdioTransportChannelFactory channel_factory_;
  std::unique_ptr<IStdioTransportChannel> channel_;
};

}  // namespace dasall::tools::mcp
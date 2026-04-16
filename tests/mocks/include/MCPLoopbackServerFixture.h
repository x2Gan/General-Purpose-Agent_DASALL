#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mcp/StdioMCPServerLauncher.h"

namespace dasall::tests::mocks {

enum class MCPLoopbackExitMode {
  stay_connected = 0,
  close_after_write = 1,
  close_after_read = 2,
};

struct MCPLoopbackFrame {
  std::string expect_write_contains;
  std::optional<std::string> response_json;
  MCPLoopbackExitMode exit_mode = MCPLoopbackExitMode::stay_connected;
};

struct MCPLoopbackScenario {
  std::string connection_id = "mcp.loopback.connection";
  std::vector<MCPLoopbackFrame> frames;
};

class MCPLoopbackServerFixture {
 public:
  explicit MCPLoopbackServerFixture(MCPLoopbackScenario scenario)
      : state_(std::make_shared<State>()) {
    state_->scenario = std::move(scenario);
  }

  [[nodiscard]] dasall::tools::mcp::StdioLaunchChannelBuilder build_channel_builder() const {
    auto state = state_;
    return [state](const dasall::tools::mcp::MCPServerSpec& spec,
                   const dasall::tools::mcp::StdioMCPLaunchSample& sample)
               -> std::unique_ptr<dasall::tools::mcp::IStdioTransportChannel> {
      return std::make_unique<LoopbackChannel>(std::move(state), spec.server_id, sample.command);
    };
  }

  [[nodiscard]] const std::vector<std::string>& written_messages() const {
    return state_->written_messages;
  }

  [[nodiscard]] const std::string& last_server_id() const {
    return state_->last_server_id;
  }

  [[nodiscard]] const std::string& last_command() const {
    return state_->last_command;
  }

  [[nodiscard]] int open_count() const {
    return state_->open_count;
  }

  [[nodiscard]] bool closed() const {
    return state_->closed;
  }

  [[nodiscard]] bool completed() const {
    return state_->frame_index >= state_->scenario.frames.size();
  }

 private:
  struct State {
    MCPLoopbackScenario scenario;
    std::vector<std::string> written_messages;
    std::string last_server_id;
    std::string last_command;
    std::size_t frame_index = 0U;
    int open_count = 0;
    bool connected = false;
    bool closed = false;
  };

  class LoopbackChannel final : public dasall::tools::mcp::IStdioTransportChannel {
   public:
    LoopbackChannel(std::shared_ptr<State> state,
                    std::string server_id,
                    std::string command)
        : state_(std::move(state)),
          server_id_(std::move(server_id)),
          command_(std::move(command)) {}

    [[nodiscard]] dasall::tools::mcp::TransportConnectResult open(
        const dasall::tools::mcp::MCPServerSpec& spec) override {
      state_->last_server_id = spec.server_id;
      state_->last_command = command_;
      state_->open_count += 1;
      state_->connected = true;
      state_->closed = false;
      connected_ = true;

      return dasall::tools::mcp::TransportConnectResult{
          .connected = true,
          .connection_id = state_->scenario.connection_id.empty()
                               ? std::optional<std::string>(std::string("mcp.loopback.connection"))
                               : std::optional<std::string>(state_->scenario.connection_id),
          .error = std::nullopt,
      };
    }

    void write(std::string_view json_rpc_message) override {
      if (!connected_) {
        throw std::runtime_error("mcp.loopback.not_connected");
      }
      if (state_->frame_index >= state_->scenario.frames.size()) {
        throw std::runtime_error("mcp.loopback.unexpected_write:" + server_id_);
      }

      const auto& frame = state_->scenario.frames[state_->frame_index];
      const std::string message(json_rpc_message);
      state_->written_messages.push_back(message);

      if (!frame.expect_write_contains.empty() &&
          message.find(frame.expect_write_contains) == std::string::npos) {
        throw std::runtime_error(
            std::string("mcp.loopback.write_mismatch expected=") +
            frame.expect_write_contains + " actual=" + message);
      }

      if (!frame.response_json.has_value() ||
          frame.exit_mode == MCPLoopbackExitMode::close_after_write) {
        advance_after_write(frame.exit_mode);
      }
    }

    [[nodiscard]] std::optional<std::string> read(std::chrono::milliseconds timeout) override {
      static_cast<void>(timeout);

      if (!connected_ || state_->frame_index >= state_->scenario.frames.size()) {
        return std::nullopt;
      }

      const auto& frame = state_->scenario.frames[state_->frame_index];
      if (!frame.response_json.has_value()) {
        return std::nullopt;
      }

      const auto response = frame.response_json;
      advance_after_read(frame.exit_mode);
      return response;
    }

    void close() override {
      connected_ = false;
      state_->connected = false;
      state_->closed = true;
    }

    [[nodiscard]] bool connected() const override {
      return connected_;
    }

   private:
    void advance_after_write(MCPLoopbackExitMode exit_mode) {
      state_->frame_index += 1U;
      if (exit_mode == MCPLoopbackExitMode::close_after_write) {
        close();
      }
    }

    void advance_after_read(MCPLoopbackExitMode exit_mode) {
      state_->frame_index += 1U;
      if (exit_mode == MCPLoopbackExitMode::close_after_read) {
        close();
      }
    }

    std::shared_ptr<State> state_;
    std::string server_id_;
    std::string command_;
    bool connected_ = false;
  };

  std::shared_ptr<State> state_;
};

}  // namespace dasall::tests::mocks
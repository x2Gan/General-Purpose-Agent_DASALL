#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "CliCommandParser.h"
#include "daemon/DaemonProtocolTypes.h"

namespace dasall::apps::cli {

class CliRequestBuilder {
 public:
  [[nodiscard]] static std::optional<dasall::access::daemon::UdsRequestFrame>
  build(const CliCommand& command) {
    if (command.name.empty() || command.name == "help" ||
        command.name == "version") {
      return std::nullopt;
    }

    dasall::access::daemon::UdsRequestFrame frame;
    frame.request_id = make_request_id(command);
    frame.trace_id = make_trace_id(command, frame.request_id);
    frame.session_hint = command.session_hint;
    frame.command = command.name == "submit" ? "run" : command.name;
    frame.async_preference = map_async_preference(command.async_preference);
    frame.output_mode = map_output_mode(command.output_mode);
    frame.deadline_ms = command.timeout_ms;

    if (frame.command == "ping" || frame.command == "readiness") {
      return frame;
    }

    if (frame.command == "run") {
      if (!command.payload.has_value()) {
        return std::nullopt;
      }

      frame.payload = *command.payload;
      frame.idempotency_key = frame.request_id;
      return frame;
    }

    if (frame.command == "status" || frame.command == "cancel") {
      return build_status_like_frame(command, frame);
    }

    if (frame.command == "diag") {
      if (!command.diag_command.has_value() || command.diag_command->empty()) {
        return std::nullopt;
      }

      frame.payload = std::string("command_name=") +
                      canonical_diag_command(*command.diag_command);
      return frame;
    }

    return std::nullopt;
  }

 private:
  [[nodiscard]] static std::string make_default_request_id(
      const std::string_view command_name) {
    return std::string("cli-") + std::string(command_name);
  }

  [[nodiscard]] static std::string make_request_id(const CliCommand& command) {
    if (command.name == "run" && command.request_id.has_value() &&
        !command.request_id->empty()) {
      return *command.request_id;
    }

    return make_default_request_id(command.name);
  }

  [[nodiscard]] static std::string make_trace_id(
      const CliCommand& command,
      const std::string_view request_id) {
    if (command.trace_id.has_value() && !command.trace_id->empty()) {
      return *command.trace_id;
    }

    return std::string(request_id) + "-trace";
  }

  [[nodiscard]] static dasall::access::daemon::DaemonAsyncPreference
  map_async_preference(const CliAsyncPreference async_preference) {
    return async_preference == CliAsyncPreference::Async
               ? dasall::access::daemon::DaemonAsyncPreference::PreferAsync
               : dasall::access::daemon::DaemonAsyncPreference::PreferSync;
  }

  [[nodiscard]] static dasall::access::daemon::DaemonOutputMode map_output_mode(
      const CliOutputMode output_mode) {
    return output_mode == CliOutputMode::Json
               ? dasall::access::daemon::DaemonOutputMode::Json
               : dasall::access::daemon::DaemonOutputMode::Human;
  }

  [[nodiscard]] static std::string canonical_diag_command(
      const std::string_view command_name) {
    if (command_name == "health") {
      return "health.snapshot";
    }
    if (command_name == "queue") {
      return "queue.stats";
    }
    if (command_name == "threads") {
      return "thread.dump";
    }
    return std::string(command_name);
  }

  [[nodiscard]] static std::optional<dasall::access::daemon::UdsRequestFrame>
  build_status_like_frame(const CliCommand& command,
                          dasall::access::daemon::UdsRequestFrame frame) {
    if (command.selector_kind == CliSelectorKind::RequestId) {
      if (!command.selector_value.has_value() || command.selector_value->empty()) {
        return std::nullopt;
      }

      frame.args.emplace("request_id", *command.selector_value);
      frame.payload = std::string("request_id=") + *command.selector_value;
      return frame;
    }

    const std::string receipt_ref = command.selector_value.value_or(
        command.receipt_ref.value_or(std::string()));
    const std::string ownership_token =
        command.ownership_token.value_or(std::string());
    if (receipt_ref.empty() || ownership_token.empty()) {
      return std::nullopt;
    }

    frame.args.emplace("receipt_ref", receipt_ref);
    frame.args.emplace("ownership_token", ownership_token);
    frame.payload = std::string("receipt_ref=") + receipt_ref;
    if (command.actor_ref.has_value() && !command.actor_ref->empty()) {
      frame.args.emplace("actor_ref", *command.actor_ref);
      frame.payload += ";actor_ref=" + *command.actor_ref;
    }
    frame.payload += ";ownership_token=" + ownership_token;
    return frame;
  }
};

}  // namespace dasall::apps::cli
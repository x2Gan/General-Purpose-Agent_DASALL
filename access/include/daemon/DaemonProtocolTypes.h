#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "agent/AgentResult.h"

namespace dasall::access::daemon {

inline constexpr std::string_view kDaemonProtocolSchemaVersion = "1";

enum class DaemonCommandKind {
  Unknown = 0,
  Ping = 1,
  Run = 2,
  Status = 3,
  Cancel = 4,
  Readiness = 5,
  Diagnostics = 6,
};

enum class DaemonAsyncPreference {
  PreferSync = 0,
  PreferAsync = 1,
};

enum class UdsResponseDisposition {
  Rejected = 0,
  Completed = 1,
  AcceptedAsync = 2,
  NotReady = 3,
};

enum class DaemonFrameDecodeError {
  None = 0,
  EmptyPayload = 1,
  MissingSchemaVersion = 2,
  UnsupportedSchemaVersion = 3,
  MissingCommand = 4,
  UnknownCommand = 5,
  PayloadTooLarge = 6,
  MalformedEnvelope = 7,
};

[[nodiscard]] constexpr DaemonCommandKind classify_daemon_command(
    std::string_view command) {
  if (command == "ping") {
    return DaemonCommandKind::Ping;
  }
  if (command == "run" || command == "submit") {
    return DaemonCommandKind::Run;
  }
  if (command == "status") {
    return DaemonCommandKind::Status;
  }
  if (command == "cancel") {
    return DaemonCommandKind::Cancel;
  }
  if (command == "readiness") {
    return DaemonCommandKind::Readiness;
  }
  if (command == "diag" || command == "diagnostics") {
    return DaemonCommandKind::Diagnostics;
  }
  return DaemonCommandKind::Unknown;
}

[[nodiscard]] constexpr bool is_known_daemon_command(DaemonCommandKind command) {
  return command != DaemonCommandKind::Unknown;
}

[[nodiscard]] constexpr bool is_read_only_daemon_command(
    DaemonCommandKind command) {
  return command == DaemonCommandKind::Ping ||
         command == DaemonCommandKind::Status ||
         command == DaemonCommandKind::Readiness ||
         command == DaemonCommandKind::Diagnostics;
}

[[nodiscard]] constexpr bool is_mutating_daemon_command(
    DaemonCommandKind command) {
  return command == DaemonCommandKind::Run ||
         command == DaemonCommandKind::Cancel;
}

struct UdsRequestFrame {
  std::string schema_version = std::string(kDaemonProtocolSchemaVersion);
  std::string request_id;
  std::string trace_id;
  std::optional<std::string> session_hint;
  std::optional<std::string> idempotency_key;
  std::string command;
  std::map<std::string, std::string> args;
  std::string payload;
  DaemonAsyncPreference async_preference = DaemonAsyncPreference::PreferSync;

  [[nodiscard]] DaemonCommandKind command_kind() const {
    return classify_daemon_command(command);
  }
};

struct UdsResponseFrame {
  std::string schema_version = std::string(kDaemonProtocolSchemaVersion);
  std::string request_id;
  std::string trace_id;
  std::optional<std::string> session_id;
  UdsResponseDisposition disposition = UdsResponseDisposition::Rejected;
  std::optional<int> exit_code_hint;
  std::optional<std::string> receipt_ref;
  std::optional<dasall::contracts::AgentResult> agent_result;
  std::optional<std::string> error_ref;
};

}  // namespace dasall::access::daemon
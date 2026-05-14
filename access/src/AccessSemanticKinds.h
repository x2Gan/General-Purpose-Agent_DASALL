#pragma once

#include <string_view>

namespace dasall::access::semantic {

enum class AccessEntryKind {
  Unknown = 0,
  Cli,
  Gateway,
  Daemon,
  Simulator,
};

enum class AccessProtocolKind {
  Unknown = 0,
  HttpUnary,
  IpcUds,
  Unix,
};

[[nodiscard]] constexpr std::string_view to_string(AccessEntryKind kind) {
  switch (kind) {
    case AccessEntryKind::Cli:
      return "cli";
    case AccessEntryKind::Gateway:
      return "gateway";
    case AccessEntryKind::Daemon:
      return "daemon";
    case AccessEntryKind::Simulator:
      return "simulator";
    case AccessEntryKind::Unknown:
      break;
  }

  return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(AccessProtocolKind kind) {
  switch (kind) {
    case AccessProtocolKind::HttpUnary:
      return "http_unary";
    case AccessProtocolKind::IpcUds:
      return "ipc_uds";
    case AccessProtocolKind::Unix:
      return "unix";
    case AccessProtocolKind::Unknown:
      break;
  }

  return "unknown";
}

[[nodiscard]] constexpr AccessEntryKind parse_access_entry_kind(std::string_view value) {
  if (value == "cli") {
    return AccessEntryKind::Cli;
  }
  if (value == "gateway") {
    return AccessEntryKind::Gateway;
  }
  if (value == "daemon") {
    return AccessEntryKind::Daemon;
  }
  if (value == "simulator") {
    return AccessEntryKind::Simulator;
  }

  return AccessEntryKind::Unknown;
}

[[nodiscard]] constexpr AccessProtocolKind parse_access_protocol_kind(
    std::string_view value) {
  if (value == "http_unary") {
    return AccessProtocolKind::HttpUnary;
  }
  if (value == "ipc_uds") {
    return AccessProtocolKind::IpcUds;
  }
  if (value == "unix") {
    return AccessProtocolKind::Unix;
  }

  return AccessProtocolKind::Unknown;
}

}  // namespace dasall::access::semantic
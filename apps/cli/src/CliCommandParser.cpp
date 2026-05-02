#include "CliCommandParser.h"

#include <string_view>
#include <vector>

namespace dasall::apps::cli {

std::optional<CliCommand> CliCommandParser::parse(int argc,
                                                   char const* const* argv) {
  if (argc < 2 || argv == nullptr) {
    return std::nullopt;
  }

  CliCommand cmd;
  // 收集原始参数（跳过 argv[0] 程序名）
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr) {
      cmd.raw_args.emplace_back(argv[i]);
    }
  }
  if (cmd.raw_args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> positional_args;
  positional_args.reserve(cmd.raw_args.size());

  for (std::size_t index = 0; index < cmd.raw_args.size(); ++index) {
    const std::string_view arg(cmd.raw_args[index]);

    if (arg.starts_with("--socket-path=")) {
      if (cmd.socket_path.has_value()) {
        return std::nullopt;
      }

      const auto value = arg.substr(std::string_view("--socket-path=").size());
      if (value.empty()) {
        return std::nullopt;
      }

      cmd.socket_path = std::string(value);
      continue;
    }

    if (arg == "--socket-path") {
      if (cmd.socket_path.has_value() || index + 1 >= cmd.raw_args.size() ||
          cmd.raw_args[index + 1].empty()) {
        return std::nullopt;
      }

      cmd.socket_path = cmd.raw_args[++index];
      continue;
    }

    positional_args.push_back(cmd.raw_args[index]);
  }

  if (positional_args.empty()) {
    return std::nullopt;
  }

  cmd.name = positional_args[0];

  if (cmd.name == "ping" || cmd.name == "readiness") {
    return cmd;
  }

  if (cmd.name == "run" || cmd.name == "submit") {
    if (positional_args.size() < 2 || positional_args[1].empty()) {
      return std::nullopt;
    }
    cmd.name = "run";
    cmd.payload = positional_args[1];
    return cmd;
  }

  if (cmd.name == "status" || cmd.name == "cancel") {
    if (positional_args.size() < 3 || positional_args[1].empty() ||
        positional_args[2].empty()) {
      return std::nullopt;
    }

    cmd.receipt_ref = positional_args[1];
    cmd.ownership_token = positional_args[2];
    if (positional_args.size() >= 4 && !positional_args[3].empty()) {
      cmd.actor_ref = positional_args[3];
    }
    return cmd;
  }

  if (cmd.name == "diag" || cmd.name == "diagnostics") {
    if (positional_args.size() < 2 || positional_args[1].empty()) {
      return std::nullopt;
    }

    cmd.name = "diag";
    cmd.diag_command = positional_args[1];
    return cmd;
  }

  // 未知命令
  return std::nullopt;
}

std::string CliCommandParser::usage_string() {
  return "Usage: dasall_cli [--socket-path <path>] <command> [args]\n"
         "Options:\n"
         "  --socket-path <path>                      Override daemon unix socket path\n"
         "Commands:\n"
         "  ping                                      Send a health check to dasall daemon\n"
         "  readiness                                 Read daemon readiness summary\n"
         "  run <json_payload>                        Submit a request to dasall daemon\n"
         "  submit <json_payload>                     Backward-compatible alias for run\n"
         "  status <receipt_ref> <ownership_token>    Query async receipt status\n"
         "  cancel <receipt_ref> <ownership_token>    Cancel an async receipt\n";
}

}  // namespace dasall::apps::cli

#include "CliCommandParser.h"

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

  cmd.name = cmd.raw_args[0];

  if (cmd.name == "ping" || cmd.name == "readiness") {
    return cmd;
  }

  if (cmd.name == "run" || cmd.name == "submit") {
    if (cmd.raw_args.size() < 2 || cmd.raw_args[1].empty()) {
      return std::nullopt;
    }
    cmd.name = "run";
    cmd.payload = cmd.raw_args[1];
    return cmd;
  }

  if (cmd.name == "status" || cmd.name == "cancel") {
    if (cmd.raw_args.size() < 3 || cmd.raw_args[1].empty() ||
        cmd.raw_args[2].empty()) {
      return std::nullopt;
    }

    cmd.receipt_ref = cmd.raw_args[1];
    cmd.ownership_token = cmd.raw_args[2];
    if (cmd.raw_args.size() >= 4 && !cmd.raw_args[3].empty()) {
      cmd.actor_ref = cmd.raw_args[3];
    }
    return cmd;
  }

  if (cmd.name == "diag" || cmd.name == "diagnostics") {
    if (cmd.raw_args.size() < 2 || cmd.raw_args[1].empty()) {
      return std::nullopt;
    }

    cmd.name = "diag";
    cmd.diag_command = cmd.raw_args[1];
    return cmd;
  }

  // 未知命令
  return std::nullopt;
}

std::string CliCommandParser::usage_string() {
  return "Usage: dasall_cli <command> [args]\n"
         "Commands:\n"
         "  ping                                      Send a health check to dasall daemon\n"
         "  readiness                                 Read daemon readiness summary\n"
         "  run <json_payload>                        Submit a request to dasall daemon\n"
         "  submit <json_payload>                     Backward-compatible alias for run\n"
         "  status <receipt_ref> <ownership_token>    Query async receipt status\n"
         "  cancel <receipt_ref> <ownership_token>    Cancel an async receipt\n";
}

}  // namespace dasall::apps::cli

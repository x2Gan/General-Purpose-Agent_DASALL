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

  if (cmd.name == "ping") {
    // ping 不需要额外参数
    return cmd;
  }

  if (cmd.name == "submit") {
    if (cmd.raw_args.size() < 2 || cmd.raw_args[1].empty()) {
      return std::nullopt;
    }
    cmd.payload = cmd.raw_args[1];
    return cmd;
  }

  // 未知命令
  return std::nullopt;
}

std::string CliCommandParser::usage_string() {
  return "Usage: dasall_cli <command> [args]\n"
         "Commands:\n"
         "  ping                   Send a health check to dasall daemon\n"
         "  submit <json_payload>  Submit a request to dasall daemon\n";
}

}  // namespace dasall::apps::cli

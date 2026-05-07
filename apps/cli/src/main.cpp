/// apps/cli/src/main.cpp
///
/// DASALL CLI 纯客户端组合根
///
/// 职责：
///   - 解析命令行参数（CliCommandParser）
///   - 通过 CliIpcClient 向 daemon 发送 UDS 请求
///   - 格式化并打印结果（CliOutputFormatter）
///
/// 架构约束：
///   - CLI 为纯客户端：不 direct link runtime，不持有 AccessGateway
///   - 所有请求经 IIPC/UDS 进入 daemon/access 主链（ADR-007/008 边界）
///   - fail-closed：连接失败 / 解析失败均以非零退出码退出

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "CliCommandParser.h"
#include "CliExitDecision.h"
#include "CliIpcClient.h"
#include "CliOutputFormatter.h"
#include "daemon/DaemonEndpointDefaults.h"
#include "linux/UnixIpcProvider.h"

namespace {

constexpr std::string_view kCliLocalVersion = "v1";
constexpr std::string_view kCliOutputSchemaVersion = "cli.output.v1";
constexpr std::string_view kCliBuildMetadata = "build-metadata-unavailable";

[[nodiscard]] std::string format_version_human_output() {
  return std::string("dasall-cli ") + std::string(kCliLocalVersion) +
         "\nschema support: " + std::string(kCliOutputSchemaVersion) +
         "\nbuild metadata: " + std::string(kCliBuildMetadata);
}

[[nodiscard]] dasall::apps::cli::DaemonClientResponse make_version_response() {
  dasall::apps::cli::DaemonClientResponse response;
  response.transport_ok = true;
  response.parse_ok = true;
  response.disposition = dasall::access::daemon::UdsResponseDisposition::Completed;
  response.response_text = std::string("dasall-cli v1; schema_support=") +
                           std::string(kCliOutputSchemaVersion) +
                           "; build_metadata=" + std::string(kCliBuildMetadata);
  response.task_completed = true;
  return response;
}

}  // namespace

int main(int argc, char* argv[]) {
  // 1. 解析命令行参数
  const auto cmd = dasall::apps::cli::CliCommandParser::parse(argc, argv);
  if (!cmd.has_value()) {
    std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                     "invalid arguments")
              << '\n';
    std::cerr << dasall::apps::cli::CliCommandParser::usage_string();
    return dasall::apps::cli::make_argument_error_decision().exit_code;
  }

  if (cmd->name == "help") {
    const std::string_view command_name =
        cmd->help_path.empty() ? std::string_view{} : cmd->help_path[0];
    const std::string_view subcommand_name =
        cmd->help_path.size() >= 2 ? std::string_view(cmd->help_path[1])
                                   : std::string_view{};
    std::cout << dasall::apps::cli::CliCommandParser::usage_string(
                     command_name, subcommand_name);
    return EXIT_SUCCESS;
  }

  if (cmd->name == "version") {
    const auto response = make_version_response();
    const auto decision = dasall::apps::cli::decide_exit_for_response(
        response, cmd->output_mode);
    if (cmd->output_mode == dasall::apps::cli::CliOutputMode::Json) {
      std::cout << dasall::apps::cli::CliOutputFormatter::format_json_output(
                       "version", response, decision)
                << '\n';
    } else {
      std::cout << format_version_human_output() << '\n';
    }
    return decision.exit_code;
  }

  // 2. 构造 IIPC provider 和 CliIpcClient
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();
  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = cmd->socket_path.value_or(
      dasall::access::daemon::kDefaultDaemonSocketPath);

    const dasall::apps::cli::CliIpcClient client(
      ipc, endpoint, cmd->timeout_ms.value_or(1000));
    const auto emit_daemon_response = [&](std::string_view command_name,
                                          const dasall::apps::cli::DaemonClientResponse& response) {
      const auto decision = dasall::apps::cli::decide_exit_for_response(
          response, cmd->output_mode);

      if (cmd->output_mode == dasall::apps::cli::CliOutputMode::Json) {
        std::cout << dasall::apps::cli::CliOutputFormatter::format_json_output(
                         command_name, response, decision)
                  << '\n';
        return decision.exit_code;
      }

      std::ostream& stream =
          decision.primary_output_stream ==
                  dasall::apps::cli::CliPrimaryOutputStream::Stdout
              ? std::cout
              : std::cerr;

      if (!response.ok()) {
        if (command_name == "ping") {
          stream << dasall::apps::cli::CliOutputFormatter::format_ping_failure(
                        response.failure_reason)
                 << '\n';
        } else {
          stream << dasall::apps::cli::CliOutputFormatter::format_error(
                        response.failure_reason)
                 << '\n';
        }
        return decision.exit_code;
      }

      stream << dasall::apps::cli::CliOutputFormatter::format_command_human_output(
                    command_name, response)
             << '\n';
      return decision.exit_code;
  };

  // 3. 执行命令
  if (cmd->name == "ping") {
    const auto response = client.invoke(*cmd);
      return emit_daemon_response("ping", response);
  }

  if (cmd->name == "readiness") {
    const auto response = client.invoke(*cmd);
      return emit_daemon_response("readiness", response);
  }

  if (cmd->name == "run") {
    const auto response = client.invoke(*cmd);
      return emit_daemon_response("run", response);
  }

  if (cmd->name == "status") {
    const auto response = client.invoke(*cmd);
      return emit_daemon_response("status", response);
  }

  if (cmd->name == "cancel") {
    const auto response = client.invoke(*cmd);
      return emit_daemon_response("cancel", response);
  }

  if (cmd->name == "diag") {
    const auto response = client.invoke(*cmd);
      return emit_daemon_response("diag", response);
  }

  // 不可达（parse 已验证命令名合法）
  return EXIT_FAILURE;
}

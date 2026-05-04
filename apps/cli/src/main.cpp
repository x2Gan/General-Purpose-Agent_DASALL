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

[[nodiscard]] std::string format_version_output(
    const dasall::apps::cli::CliOutputMode output_mode) {
  if (output_mode == dasall::apps::cli::CliOutputMode::Json) {
    return std::string("{\"schema_version\":\"") +
           std::string(kCliOutputSchemaVersion) +
           "\",\"command\":\"version\",\"request_id\":null,"
           "\"trace_id\":null,\"session_id\":null,"
           "\"disposition\":\"completed\",\"receipt_ref\":null,"
           "\"result\":{\"response_text\":\"dasall_cli v1; schema_support=cli.output.v1; "
           "build_metadata=build-metadata-unavailable\",\"task_completed\":true},"
           "\"error\":null,\"warnings\":[],\"exit_code\":0}";
  }

  return std::string("dasall_cli ") + std::string(kCliLocalVersion) +
         "\nschema support: " + std::string(kCliOutputSchemaVersion) +
         "\nbuild metadata: " + std::string(kCliBuildMetadata);
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
    std::cout << format_version_output(cmd->output_mode) << '\n';
    return EXIT_SUCCESS;
  }

  // 2. 构造 IIPC provider 和 CliIpcClient
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();
  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = cmd->socket_path.value_or(
      dasall::access::daemon::kDefaultDaemonSocketPath);

  const dasall::apps::cli::CliIpcClient client(ipc, endpoint);
  const auto exit_code_for = [&](const dasall::apps::cli::DaemonClientResponse& response) {
    return dasall::apps::cli::decide_exit_for_response(response, cmd->output_mode)
        .exit_code;
  };

  // 3. 执行命令
  if (cmd->name == "ping") {
    const auto response = client.ping_daemon();
    if (!response.ok()) {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_ping_failure(
                       response.failure_reason)
                << '\n';
      return exit_code_for(response);
    }

    std::cout << dasall::apps::cli::CliOutputFormatter::format_ping_success(response)
              << '\n';
    return exit_code_for(response);
  }

  if (cmd->name == "readiness") {
    const auto response = client.read_readiness();
    if (!response.ok()) {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                       response.failure_reason)
                << '\n';
      return exit_code_for(response);
    }

    std::cout
        << dasall::apps::cli::CliOutputFormatter::format_readiness_success(
               response)
        << '\n';
    return exit_code_for(response);
  }

  if (cmd->name == "run") {
    const auto response = client.submit(cmd->payload.value_or("{}"));
    if (!response.ok()) {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                       response.failure_reason)
                << '\n';
      return exit_code_for(response);
    }

    std::cout
        << dasall::apps::cli::CliOutputFormatter::format_submit_success(
               response)
        << '\n';
    return exit_code_for(response);
  }

  if (cmd->name == "status") {
    const auto response = client.query_status(
        cmd->receipt_ref.value_or(""),
        cmd->ownership_token.value_or(""),
        cmd->actor_ref.value_or(""));
    if (!response.ok()) {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                       response.failure_reason)
                << '\n';
      return exit_code_for(response);
    }

    std::cout
        << dasall::apps::cli::CliOutputFormatter::format_status_success(
               response)
        << '\n';
    return exit_code_for(response);
  }

  if (cmd->name == "cancel") {
    const auto response = client.cancel(
        cmd->receipt_ref.value_or(""),
        cmd->ownership_token.value_or(""),
        cmd->actor_ref.value_or(""));
    if (!response.ok()) {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                       response.failure_reason)
                << '\n';
      return exit_code_for(response);
    }

    std::cout
        << dasall::apps::cli::CliOutputFormatter::format_cancel_success(
               response)
        << '\n';
    return exit_code_for(response);
  }

  if (cmd->name == "diag") {
    const auto response = client.run_diagnostics(cmd->diag_command.value_or(""));
    if (!response.ok()) {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                       response.failure_reason)
                << '\n';
      return exit_code_for(response);
    }

    std::cout << dasall::apps::cli::CliOutputFormatter::format_diag_success(
                     response)
              << '\n';
    return exit_code_for(response);
  }

  // 不可达（parse 已验证命令名合法）
  return EXIT_FAILURE;
}

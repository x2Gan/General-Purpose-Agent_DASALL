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
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include <termios.h>
#include <unistd.h>

#include "CliCommandParser.h"
#include "CliExitDecision.h"
#include "CliIpcClient.h"
#include "CliOutputFormatter.h"
#include "config/CliConfigWorkflowCoordinator.h"
#include "daemon/DaemonEndpointDefaults.h"
#include "linux/UnixIpcProvider.h"

namespace {

constexpr std::string_view kCliLocalVersion = "v1";
constexpr std::string_view kCliOutputSchemaVersion = "cli.output.v1";
constexpr std::string_view kCliBuildMetadata = "build-metadata-unavailable";
constexpr std::int32_t kDefaultDaemonIpcTimeoutMs = 1000;
constexpr std::int32_t kDefaultRunIpcTimeoutMs = 120000;

[[nodiscard]] std::int32_t default_timeout_ms_for(
    const dasall::apps::cli::CliCommand& command) {
  if (command.name == "run") {
    return kDefaultRunIpcTimeoutMs;
  }

  return kDefaultDaemonIpcTimeoutMs;
}

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

[[nodiscard]] dasall::apps::cli::DaemonClientResponse make_local_error_response(
    std::string_view reason,
    const bool parse_ok) {
  dasall::apps::cli::DaemonClientResponse response;
  response.transport_ok = true;
  response.parse_ok = parse_ok;
  response.disposition = dasall::access::daemon::UdsResponseDisposition::Rejected;
  response.failure_reason = std::string(reason);
  return response;
}

[[nodiscard]] std::string_view config_command_name(
    const dasall::apps::cli::CliCommand& command) {
  using dasall::apps::cli::CliConfigCommandKind;

  switch (command.config_command) {
    case CliConfigCommandKind::Wizard:
      return "config";
    case CliConfigCommandKind::Show:
      return "config.show";
    case CliConfigCommandKind::Plan:
      return "config.plan";
    case CliConfigCommandKind::Validate:
      return "config.validate";
    case CliConfigCommandKind::Apply:
      return "config.apply";
    case CliConfigCommandKind::None:
      return "config";
  }

  return "config";
}

[[nodiscard]] std::string prompt_suffix(std::string_view default_value) {
  if (default_value.empty()) {
    return ": ";
  }

  return std::string(" [default: ") + std::string(default_value) + "]: ";
}

[[nodiscard]] std::optional<std::string> read_prompt_input(
    const dasall::apps::cli::config::PromptRequest& request) {
  std::cout << request.message << prompt_suffix(request.default_value) << std::flush;

  std::string input;
  if (request.masked) {
    termios original_mode{};
    if (::tcgetattr(STDIN_FILENO, &original_mode) == 0) {
      termios masked_mode = original_mode;
      masked_mode.c_lflag &= static_cast<unsigned long>(~ECHO);
      ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &masked_mode);
      const bool read_ok = static_cast<bool>(std::getline(std::cin, input));
      ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_mode);
      std::cout << '\n';
      if (!read_ok) {
        return std::nullopt;
      }
      return input;
    }
  }

  if (!std::getline(std::cin, input)) {
    return std::nullopt;
  }
  return input;
}

[[nodiscard]] std::optional<bool> read_confirm_input(
    const dasall::apps::cli::config::ConfirmRequest& request) {
  const std::string suffix = request.default_value ? " [Y/n]: " : " [y/N]: ";
  while (true) {
    std::cout << request.message << suffix << std::flush;
    std::string input;
    if (!std::getline(std::cin, input)) {
      return std::nullopt;
    }
    if (input.empty()) {
      return std::nullopt;
    }

    for (char& current : input) {
      current = static_cast<char>(std::tolower(static_cast<unsigned char>(current)));
    }
    if (input == "y" || input == "yes" || input == "true") {
      return true;
    }
    if (input == "n" || input == "no" || input == "false") {
      return false;
    }

    std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                     "please answer yes or no")
              << '\n';
  }
}

int emit_local_response(
    std::string_view command_name,
    const dasall::apps::cli::DaemonClientResponse& response,
    const dasall::apps::cli::CliExitDecision& decision) {
  if (decision.json_mode) {
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
  stream << dasall::apps::cli::CliOutputFormatter::format_error(
                response.failure_reason.empty() ? decision.diagnostic_hint
                                               : response.failure_reason)
         << '\n';
  return decision.exit_code;
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

  if (cmd->name == "config") {
    if (cmd->config_command == dasall::apps::cli::CliConfigCommandKind::Wizard &&
        ::isatty(STDIN_FILENO) == 0) {
      constexpr std::string_view kNonTtyConfigHint =
          "interactive config requires a TTY; use 'dasall-cli config plan --from-file <path>' or 'dasall-cli config apply --from-file <path> --no-input'";
      const auto decision = dasall::apps::cli::make_argument_error_decision(
          cmd->output_mode, kNonTtyConfigHint);
      return emit_local_response(
          config_command_name(*cmd),
          make_local_error_response(kNonTtyConfigHint, true),
          decision);
    }

        dasall::apps::cli::config::CliConfigWorkflowDependencies dependencies;
        dependencies.prompt_input_handler = read_prompt_input;
        dependencies.prompt_confirm_handler = read_confirm_input;
        const dasall::apps::cli::config::CliConfigWorkflowCoordinator coordinator(
          std::move(dependencies));
      const auto workflow_result = coordinator.run(*cmd);
      if (!workflow_result.handled) {
        constexpr std::string_view kUnhandledConfigWorkflow =
          "config local workflow dispatch failed to handle command";
        const auto response = make_local_error_response(kUnhandledConfigWorkflow,
                                false);
        const auto decision = dasall::apps::cli::decide_exit_for_response(
          response, cmd->output_mode);
        return emit_local_response(config_command_name(*cmd), response, decision);
      }

      std::ostream& stream =
        cmd->output_mode == dasall::apps::cli::CliOutputMode::Json ||
            workflow_result.success
          ? std::cout
          : std::cerr;
      stream << workflow_result.output << '\n';
      return workflow_result.exit_code;
  }

  // 2. 构造 IIPC provider 和 CliIpcClient
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();
  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = cmd->socket_path.value_or(
      dasall::access::daemon::kDefaultDaemonSocketPath);

  auto effective_cmd = *cmd;
  if (!effective_cmd.timeout_ms.has_value()) {
    effective_cmd.timeout_ms = default_timeout_ms_for(effective_cmd);
  }

    const dasall::apps::cli::CliIpcClient client(
      ipc, endpoint, *effective_cmd.timeout_ms);
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
  if (effective_cmd.name == "ping") {
    const auto response = client.invoke(effective_cmd);
      return emit_daemon_response("ping", response);
  }

  if (effective_cmd.name == "readiness") {
    const auto response = client.invoke(effective_cmd);
      return emit_daemon_response("readiness", response);
  }

  if (effective_cmd.name == "run") {
    const auto response = client.invoke(effective_cmd);
      return emit_daemon_response("run", response);
  }

  if (effective_cmd.name == "status") {
    const auto response = client.invoke(effective_cmd);
      return emit_daemon_response("status", response);
  }

  if (effective_cmd.name == "cancel") {
    const auto response = client.invoke(effective_cmd);
      return emit_daemon_response("cancel", response);
  }

  if (effective_cmd.name == "diag") {
    const auto response = client.invoke(effective_cmd);
      return emit_daemon_response("diag", response);
  }

  // 不可达（parse 已验证命令名合法）
  return EXIT_FAILURE;
}

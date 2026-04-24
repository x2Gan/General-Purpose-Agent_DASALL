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

#include "CliCommandParser.h"
#include "CliIpcClient.h"
#include "CliOutputFormatter.h"
#include "linux/UnixIpcProvider.h"

int main(int argc, char* argv[]) {
  // 1. 解析命令行参数
  const auto cmd = dasall::apps::cli::CliCommandParser::parse(argc, argv);
  if (!cmd.has_value()) {
    std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                     "invalid arguments")
              << '\n';
    std::cerr << dasall::apps::cli::CliCommandParser::usage_string();
    return EXIT_FAILURE;
  }

  // 2. 构造 IIPC provider 和 CliIpcClient
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();
  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  const dasall::apps::cli::CliIpcClient client(ipc, endpoint);

  // 3. 执行命令
  if (cmd->name == "ping") {
    const bool ok = client.ping_daemon();
    if (ok) {
      std::cout << dasall::apps::cli::CliOutputFormatter::format_ping_success("")
                << '\n';
      return EXIT_SUCCESS;
    } else {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_ping_failure()
                << '\n';
      return EXIT_FAILURE;
    }
  }

  if (cmd->name == "submit") {
    const bool ok = client.send_payload(cmd->payload.value_or("{}"));
    if (ok) {
      std::cout << dasall::apps::cli::CliOutputFormatter::format_submit_success("")
                << '\n';
      return EXIT_SUCCESS;
    } else {
      std::cerr << dasall::apps::cli::CliOutputFormatter::format_error(
                       "submit failed — daemon unavailable")
                << '\n';
      return EXIT_FAILURE;
    }
  }

  // 不可达（parse 已验证命令名合法）
  return EXIT_FAILURE;
}

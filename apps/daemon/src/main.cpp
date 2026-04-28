/// apps/daemon/src/main.cpp
///
/// DASALL daemon 本地控制面组合根
///
/// 职责：
///   - 构造所有依赖（IIPC provider、AccessGateway）
///   - 初始化 AccessGateway（注入最小 SubmitPipeline 和 PublishBackend）
///   - 构造 DaemonBootstrap 并调用 run() 进入事件循环
///
/// 架构约束：
///   - Daemon 是 Access 主链的本地 owner，不持有 runtime 内部控制权
///   - 所有客户端请求经 AccessGateway 主链（Admission/Policy/Normalizer/RuntimeBridge）
///   - 本地控制面优先于 HTTP/gateway；CLI 经 IIPC/UDS 进入本链路

#include <csignal>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "DaemonBootstrap.h"
#include "DaemonConfig.h"
#include "DaemonConfigValidator.h"
#include "DaemonSignalHandler.h"
#include "IAccessGateway.h"
#include "linux/UnixIpcProvider.h"

// AccessGateway 是 internal 实现，通过 access/src 路径包含
// （CMakeLists.txt 已将 access/src 加入 PRIVATE include dirs）
#include "AccessGateway.h"

namespace {

struct ParsedDaemonArgs {
  dasall::apps::daemon::DaemonBootstrapConfig config;
  bool validate_only = false;
  bool ok = true;
  std::string error;
};

[[nodiscard]] ParsedDaemonArgs parse_daemon_args(int argc, char* argv[]) {
  ParsedDaemonArgs parsed;

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);

    if (arg == "--validate-only") {
      parsed.validate_only = true;
      continue;
    }

    if (arg.starts_with("--socket-path=")) {
      parsed.config.socket_path =
          std::string(arg.substr(std::string_view("--socket-path=").size()));
      continue;
    }

    if (arg == "--socket-path") {
      if (index + 1 >= argc) {
        parsed.ok = false;
        parsed.error = "--socket-path requires a value";
        return parsed;
      }

      parsed.config.socket_path = argv[++index];
      continue;
    }

    parsed.ok = false;
    parsed.error = std::string("unsupported argument: ") + std::string(arg);
    return parsed;
  }

  return parsed;
}

}  // namespace

int main(int argc, char* argv[]) {
  const ParsedDaemonArgs parsed = parse_daemon_args(argc, argv);
  if (!parsed.ok) {
    std::cerr << "[dasall_daemon] argument parse failed: " << parsed.error << "\n";
    return 1;
  }

  const dasall::apps::daemon::DaemonConfigValidator config_validator;
  const auto validation = parsed.validate_only
                              ? config_validator.validate_only(parsed.config)
                              : config_validator.validate_config(parsed.config);
  if (!validation.ok()) {
    std::cerr << "[dasall_daemon] config validation failed: "
              << validation.message << "\n";
    return 1;
  }

  if (parsed.validate_only) {
    std::cout << "[dasall_daemon] " << validation.message << "\n";
    return 0;
  }

  // 1. 创建 IIPC provider（Linux UDS 实现）
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();

  // 2. 构造 AccessGateway（v1: 使用空 submit pipeline 和 publish backend 占位）
  //    注：实际生产中需注入完整的主链 pipeline（含 Validator / Admission / RuntimeBridge）
  auto gateway = std::make_shared<dasall::access::AccessGateway>();
  if (!gateway->init()) {
    std::cerr << "[dasall_daemon] AccessGateway init failed\n";
    return 1;
  }

  const auto context = dasall::apps::daemon::DaemonBootstrap::build(
      parsed.config,
      dasall::apps::daemon::DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.direct_bind.v1",
          .config_revision = std::nullopt,
      });
  if (!context.has_value()) {
    std::cerr << "[dasall_daemon] bootstrap build failed\n";
    return 1;
  }

  // 3. 构造 DaemonBootstrap（通过 build(config) 产出的只读 process context 驱动）
  dasall::apps::daemon::DaemonBootstrap bootstrap;
  dasall::apps::daemon::DaemonSignalHandler signal_handler;
  if (!signal_handler.install_handlers()) {
    std::cerr << "[dasall_daemon] signal handler install failed\n";
    return 1;
  }

  // 4. 启动 UDS 服务端
  std::cout << "[dasall_daemon] starting on "
            << context->bootstrap_config.socket_path << "\n";

  bool run_ok = false;
  std::atomic<bool> run_finished{false};
  std::thread daemon_thread([&bootstrap, &context, &run_ok, &run_finished]() {
    run_ok = bootstrap.run(*context);
    run_finished.store(true);
  });

  while (!run_finished.load()) {
    if (signal_handler.shutdown_requested()) {
      bootstrap.stop();
      break;
    }

    if (signal_handler.reload_requested()) {
      std::cout << "[dasall_daemon] reload requested by signal "
                << signal_handler.last_signal()
                << " (not applied in v1)\n";
      signal_handler.clear_requests();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  daemon_thread.join();

  // 5. 优雅关闭 AccessGateway
  gateway->shutdown(std::chrono::milliseconds(parsed.config.shutdown_grace_ms));

  std::cout << "[dasall_daemon] stopped (run=" << (run_ok ? "ok" : "failed") << ")\n";
  return run_ok ? 0 : 1;
}



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
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

#include "DaemonBootstrap.h"
#include "DaemonConfig.h"
#include "DaemonConfigValidator.h"
#include "IAccessGateway.h"
#include "linux/UnixIpcProvider.h"

// AccessGateway 是 internal 实现，通过 access/src 路径包含
// （CMakeLists.txt 已将 access/src 加入 PRIVATE include dirs）
#include "AccessGateway.h"

namespace {

/// 全局 bootstrap 指针，用于信号处理器中调用 stop()
dasall::apps::daemon::DaemonBootstrap* g_bootstrap = nullptr;

/// SIGTERM/SIGINT 信号处理器：请求优雅关闭
extern "C" void on_shutdown_signal(int /*signal*/) {
  if (g_bootstrap != nullptr) {
    g_bootstrap->stop();
  }
}

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

  // 3. 构造 DaemonBootstrap（通过 IAccessGateway 接口传入）
  dasall::apps::daemon::DaemonBootstrap bootstrap(ipc, gateway);
  g_bootstrap = &bootstrap;

  // 4. 注册信号处理器（优雅关闭）
  std::signal(SIGTERM, on_shutdown_signal);
  std::signal(SIGINT, on_shutdown_signal);

  // 5. 启动 UDS 服务端
  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = parsed.config.socket_path;

  std::cout << "[dasall_daemon] starting on " << endpoint.socket_path << "\n";
  const bool run_ok = bootstrap.run(endpoint);

  // 6. 优雅关闭 AccessGateway
  gateway->shutdown(std::chrono::milliseconds(parsed.config.shutdown_grace_ms));

  std::cout << "[dasall_daemon] stopped (run=" << (run_ok ? "ok" : "failed") << ")\n";
  return run_ok ? 0 : 1;
}



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

#include "DaemonBootstrap.h"
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

}  // namespace

int main() {
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
  endpoint.socket_path = "/tmp/dasall-daemon-control.sock";

  std::cout << "[dasall_daemon] starting on " << endpoint.socket_path << "\n";
  const bool run_ok = bootstrap.run(endpoint);

  // 6. 优雅关闭 AccessGateway
  gateway->shutdown(std::chrono::milliseconds(5000));

  std::cout << "[dasall_daemon] stopped (run=" << (run_ok ? "ok" : "failed") << ")\n";
  return run_ok ? 0 : 1;
}



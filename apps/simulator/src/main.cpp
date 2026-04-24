/// apps/simulator/src/main.cpp
///
/// DASALL simulator entry-point composition root
///
/// Responsibilities:
///   - 创建 SimulatorProtocolAdapter 初始化实例
///   - 在测试/工厂 profile 下启用 simulator 入口
///   - 管理 fixture 注入的确定性测试刺激
///
/// Design Constraints (Access 详设 6.14.9, 6.15.3, 8.1):
///   - simulator 入口仅在测试/工厂 profile 可用
///   - 所有测试输入通过 DeterministicSubjectStub fixture 装配注入
///   - 行为可重复：相同 fixture + 相同请求 = 相同结果
///
/// Non-Responsibilities:
///   - 不执行实际 runtime 语义；仅返回 AcceptedAsync pending 状态
///   - 不复制 Admission/Normalizer/RuntimeBridge/ResultPublisher（复用共享 core）
#include <chrono>
#include <iostream>
#include <memory>
#include <signal.h>
#include <thread>

#include "SimulatorProtocolAdapter.h"

using namespace dasall::access::simulator;

// 全局标志位用于优雅关闭
volatile sig_atomic_t shutdown_requested = 0;

void signal_handler(int sig) {
  if (sig == SIGTERM || sig == SIGINT) {
    shutdown_requested = 1;
  }
}

int main() {
  // 注册信号处理器
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  try {
    // 创建 fixture 注入的确定性主体
    DeterministicSubjectStub deterministic_subject{
        .actor_ref = "simulator_test_actor",
        .granted_actions = {"read", "write", "execute"},
        .override_source = ""  // v1 不使用 override 路径
    };

    // 创建 SimulatorProtocolAdapter
    auto simulator_adapter =
        std::make_shared<SimulatorProtocolAdapter>(deterministic_subject);

    std::cout << "dasall_simulator initialized with deterministic adapter" << std::endl;
    std::cout << "Entry: simulator, Protocol: deterministic_test" << std::endl;
    std::cout << "Deterministic Subject: actor_ref=" << deterministic_subject.actor_ref
              << std::endl;
    std::cout << "Granted Actions: " << deterministic_subject.granted_actions.size()
              << std::endl;

    // v1 版本：simulator 作为被动接收者，不主动监听
    // 等待来自测试框架的请求或优雅关闭信号
    while (!shutdown_requested) {
      // 简单的空循环；实际环境中可以有事件循环或 IPC listener
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "dasall_simulator shutdown complete" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "simulator fatal error: " << e.what() << std::endl;
    return 1;
  }
}

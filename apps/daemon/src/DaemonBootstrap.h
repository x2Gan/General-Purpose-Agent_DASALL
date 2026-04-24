#pragma once

#include <atomic>
#include <memory>

#include "IAccessGateway.h"
#include "IIPC.h"
#include "daemon/DaemonProtocolAdapter.h"

namespace dasall::apps::daemon {

/// DaemonBootstrap — daemon 本地控制面服务端生命周期管理
///
/// 职责：
///   1. 在 UDS 端点上创建监听器并循环接受 CLI 连接
///   2. 对每条连接：receive → peer identity → decode → IAccessGateway.submit → encode
///   3. 将 LocalPeerUidFact 注入 InboundPacket 以驱动 local trusted 判定
///   4. 响应 stop() 请求，完成当前请求后退出循环
///
/// 边界约束：
///   - 单线程同步模型（v1）：每次只处理一个连接请求
///   - 通过 IAccessGateway 接口与 AccessGateway 解耦，不感知具体实现
///   - 优雅关闭：stop() 设置标志，run() 在下次 accept 超时后退出
class DaemonBootstrap {
 public:
  /// 构造 DaemonBootstrap。
  /// @param ipc       IIPC 实现，用于监听和 accept
  /// @param gateway   已初始化的 IAccessGateway 实现（调用者负责 init()）
  DaemonBootstrap(std::shared_ptr<dasall::platform::IIPC> ipc,
                  std::shared_ptr<dasall::access::IAccessGateway> gateway);

  /// 在指定端点启动监听并进入事件循环（阻塞直到 stop() 被调用）。
  /// @return 若监听启动失败返回 false；正常退出返回 true
  [[nodiscard]] bool run(const dasall::platform::IpcEndpoint& endpoint);

  /// 请求停止事件循环。线程安全。
  void stop();

 private:
  /// 处理单条已接受的连接（receive → decode → submit → encode）。
  /// @return 若成功完成整个处理流程返回 true
  [[nodiscard]] bool handle_connection(
      const dasall::platform::IpcChannelHandle& channel);

  /// 构建 ping 响应 JSON。
  [[nodiscard]] static std::string make_ping_response();

  std::shared_ptr<dasall::platform::IIPC> ipc_;
  std::shared_ptr<dasall::access::IAccessGateway> gateway_;
  std::atomic<bool> stop_requested_{false};

  // accept 超时（毫秒）：控制退出检测频率
  static constexpr std::int32_t kAcceptDeadlineMs = 500;

  // receive 超时（毫秒）
  static constexpr std::int32_t kReceiveDeadlineMs = 5000;

  // 最大 payload 字节数（防止超大请求）
  static constexpr std::uint32_t kMaxPayloadBytes = 1048576;
};

}  // namespace dasall::apps::daemon


#pragma once

#include <chrono>

#include "AccessTypes.h"

namespace dasall::access {

class IAccessGateway {
 public:
  virtual ~IAccessGateway() = default;

  // 初始化与请求处理接口
  virtual bool init() = 0;
  virtual RuntimeDispatchResult submit(const InboundPacket& packet) = 0;
  virtual bool publish_result(const PublishEnvelope& envelope) = 0;

  // 生命周期管理接口
  // 获取当前的生命周期状态
  virtual AccessGatewayState state() const = 0;

  // 快速判断网关是否就绪（可接受新请求）
  // 当且仅当 state() == AccessGatewayState::Ready 时返回 true
  virtual bool is_ready() const = 0;

  // 触发优雅排空和关闭
  // 参数 drain_timeout: 从调用时起，最多等待该时长让 inflight 请求完成
  // 转移过程: Ready -> Draining -> ShutDown
  // 在 Draining 期间，新请求返回 ShuttingDown 错误，但已受理请求可继续发布
  // 超时后强制关闭并写审计事件
  virtual void shutdown(std::chrono::milliseconds drain_timeout) = 0;
};

}  // namespace dasall::access
#pragma once

#include <memory>

#include "AccessTypes.h"

namespace dasall::access {
class AsyncTaskRegistry;  // forward declaration
}  // namespace dasall::access

namespace dasall::access::daemon {

/// DaemonResponseBuilderWithReceipt — daemon 响应构建器辅助，支持 accepted_async 回执
///
/// 职责：
///   1. 对 accepted_async 结果调用 AsyncTaskRegistry::register_async_accept()
///   2. 生成并返回 receipt 信息，供 DaemonProtocolAdapter 注入 PublishEnvelope
///   3. 提供统一的 register_and_build_receipt() 接口
///
/// 边界约束：
///   - 仅处理 receipt 生成，不处理结果发布
///   - receipt 生成只在 disposition == AcceptedAsync 时触发
///   - 如果 AsyncTaskRegistry unavailable，gracefully fallback（返回 null receipt）
///   - 不创建新的任务系统，仅保存映射和元数据
class DaemonResponseBuilderWithReceipt final {
 public:
  /// 构造函数
  /// @param registry 异步任务注册表（可为空，为空时 receipt 功能禁用）
  explicit DaemonResponseBuilderWithReceipt(
      std::shared_ptr<dasall::access::AsyncTaskRegistry> registry);

  /// 注册异步受理并生成 receipt
  /// @param request 原始请求信息
  /// @param runtime_result Runtime 返回的 dispatch result（包含 disposition）
  /// @return receipt 指针；如果非 AcceptedAsync 或无 registry，返回 nullptr
  [[nodiscard]] std::shared_ptr<AsyncTaskReceipt> register_and_build_receipt(
      const RuntimeDispatchRequest& request,
      const RuntimeDispatchResult& runtime_result) const;

 private:
  std::shared_ptr<dasall::access::AsyncTaskRegistry> registry_;
};

}  // namespace dasall::access::daemon

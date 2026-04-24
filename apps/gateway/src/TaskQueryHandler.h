/// apps/gateway/src/TaskQueryHandler.h
///
/// DASALL gateway task receipt query 与 cancel 处理器（v1）
///
/// 职责：
///   - 处理 GET /v1/receipt/{receipt_id}：验证 ownership → 返回 pending/completed/expired 状态
///   - 处理 POST /v1/cancel/{receipt_id}：验证 ownership → 标记 cancelled
///   - 不经过完整 Admission pipeline（已由原始 submit 做过准入，query/cancel 仅做 ownership 校验）
///
/// 设计约束（Access 详设 6.18.2、6.19.2）：
///   - ownership 校验双因子：actor_ref + ownership_token
///   - owner mismatch → fail-closed，不暴露原始 actor 信息
///   - receipt 过期 → 明确 ReceiptExpired，不返回模糊 not found
///   - v1 cancel：仅在 registry 标记 cancelled；RuntimeBridge cancel 转发在 Phase A4 补充
#pragma once

#include <string>
#include <string_view>

namespace dasall::access {
class AsyncTaskRegistry;
}  // namespace dasall::access

namespace dasall::access::gateway {

/// TaskQueryHandler 响应结构
struct QueryHandlerResult {
  enum class Status {
    Ok = 0,          ///< 查询/取消成功
    NotFound = 1,    ///< receipt 不存在
    Expired = 2,     ///< receipt 已过期
    OwnerMismatch = 3,  ///< owner 不匹配（fail-closed）
    Cancelled = 4,   ///< 任务已 cancelled（cancel 成功 或 query 时）
  };

  Status status{Status::NotFound};
  std::string task_status;   ///< "pending" / "completed" / "expired" / "cancelled"
  std::string receipt_id;
};

/// TaskQueryHandler — 无 httplib 依赖的纯查询/取消逻辑
class TaskQueryHandler {
 public:
  explicit TaskQueryHandler(dasall::access::AsyncTaskRegistry& registry)
      : registry_(registry) {}

  /// 查询 receipt 状态：先校验 ownership，再返回 task_status
  ///
  /// @param receipt_id    receipt 唯一 ID
  /// @param actor_ref     调用者主体标识（须与原始 actor_ref 一致）
  /// @param ownership_token  HMAC 所有权令牌
  [[nodiscard]] QueryHandlerResult handle_query(
      std::string_view receipt_id,
      std::string_view actor_ref,
      std::string_view ownership_token) const;

  /// 取消任务：先校验 ownership，再将 receipt 标记为 cancelled
  ///
  /// v1 仅在 registry 侧标记；RuntimeBridge cancel 转发在 Phase A4 补充
  [[nodiscard]] QueryHandlerResult handle_cancel(
      std::string_view receipt_id,
      std::string_view actor_ref,
      std::string_view ownership_token);

 private:
  dasall::access::AsyncTaskRegistry& registry_;
};

}  // namespace dasall::access::gateway

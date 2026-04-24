/// apps/gateway/src/TaskQueryHandler.cpp
///
/// TaskQueryHandler 实现

#include "TaskQueryHandler.h"

#include <string>

// access/src PRIVATE include（via apps/gateway CMakeLists.txt）
#include "AsyncTaskRegistry.h"

namespace dasall::access::gateway {

QueryHandlerResult TaskQueryHandler::handle_query(
    std::string_view receipt_id,
    std::string_view actor_ref,
    std::string_view ownership_token) const {
  const std::string id{receipt_id};

  // 0. 先读取 receipt 状态，确保 expired 与 not_found 语义可区分
  const auto initial_qr = registry_.query_receipt(id);
  using QS = dasall::access::AsyncTaskRegistry::QueryStatus;
  if (initial_qr.status == QS::NotFound) {
    return QueryHandlerResult{QueryHandlerResult::Status::NotFound, "not_found", id};
  }
  if (initial_qr.status == QS::Expired) {
    return QueryHandlerResult{QueryHandlerResult::Status::Expired, "expired", id};
  }

  // 1. 先校验 ownership（双因子：actor_ref + ownership_token）
  if (!registry_.validate_ownership(id, actor_ref, ownership_token)) {
    // initial_qr 已确认存在且未过期，此处仅可能是 ownership mismatch
    return QueryHandlerResult{QueryHandlerResult::Status::OwnerMismatch, "forbidden", id};
  }

  // 2. ownership 通过，返回 task_status（pending / completed / cancelled / 自定义）
  const auto& receipt = initial_qr.receipt.value();
  const std::string task_status =
      receipt.initial_status.value_or("pending");

  auto result_status = QueryHandlerResult::Status::Ok;
  if (task_status == "cancelled") {
    result_status = QueryHandlerResult::Status::Cancelled;
  }

  return QueryHandlerResult{result_status, task_status, id};
}

QueryHandlerResult TaskQueryHandler::handle_cancel(
    std::string_view receipt_id,
    std::string_view actor_ref,
    std::string_view ownership_token) {
  const std::string id{receipt_id};

  // 0. 先读取 receipt 状态，确保 expired 与 not_found 语义可区分
  const auto initial_qr = registry_.query_receipt(id);
  using QS = dasall::access::AsyncTaskRegistry::QueryStatus;
  if (initial_qr.status == QS::NotFound) {
    return QueryHandlerResult{QueryHandlerResult::Status::NotFound, "not_found", id};
  }
  if (initial_qr.status == QS::Expired) {
    return QueryHandlerResult{QueryHandlerResult::Status::Expired, "expired", id};
  }

  // 1. 校验 ownership
  if (!registry_.validate_ownership(id, actor_ref, ownership_token)) {
    return QueryHandlerResult{QueryHandlerResult::Status::OwnerMismatch, "forbidden", id};
  }

  // 2. 标记 cancelled（v1：仅在 registry 侧；RuntimeBridge cancel 转发在 Phase A4 补充）
  const bool ok = registry_.mark_completed(id, "cancelled");
  if (!ok) {
    // receipt 在 validate → mark 之间过期（极小窗口）
    return QueryHandlerResult{QueryHandlerResult::Status::Expired, "expired", id};
  }

  return QueryHandlerResult{QueryHandlerResult::Status::Cancelled, "cancelled", id};
}

}  // namespace dasall::access::gateway

#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "AsyncTaskRegistry.h"
#include "TaskQueryHandler.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-027-query";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http_unary";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["request_id"] = "req-027-query";
  request.request_context["session_id"] = "sess-027-query";
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_async_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
  result.receipt_ref = "receipt:027-query";
  return result;
}

void query_owner_receipt_returns_pending_completed_expired() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::gateway::QueryHandlerResult;
  using dasall::access::gateway::TaskQueryHandler;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("static-secret-027", std::chrono::milliseconds(5));
  TaskQueryHandler handler(registry);

  const auto receipt = registry.register_async_accept(make_request(), make_async_result());
  assert_true(receipt.has_value(), "accepted async must register a receipt");

  const auto pending = handler.handle_query(
      receipt->receipt_id, receipt->actor_ref, receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::Ok),
               static_cast<int>(pending.status),
               "owner query should return Ok status for pending task");
  assert_equal(std::string("pending"), pending.task_status,
               "new receipt should be pending");

  assert_true(registry.mark_completed(receipt->receipt_id, "completed"),
              "mark_completed should succeed for active receipt");
  const auto completed = handler.handle_query(
      receipt->receipt_id, receipt->actor_ref, receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::Ok),
               static_cast<int>(completed.status),
               "owner query should stay Ok when task is completed");
  assert_equal(std::string("completed"), completed.task_status,
               "query should return completed after mark_completed");

  // 等待 TTL 到期，验证 expired 路径
  std::this_thread::sleep_for(std::chrono::milliseconds(8));
  const auto expired = handler.handle_query(
      receipt->receipt_id, receipt->actor_ref, receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::Expired),
               static_cast<int>(expired.status),
               "query should return Expired after TTL");
  assert_equal(std::string("expired"), expired.task_status,
               "expired query should carry expired task_status");
}

void non_owner_query_and_cancel_are_rejected() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::gateway::QueryHandlerResult;
  using dasall::access::gateway::TaskQueryHandler;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("static-secret-027");
  TaskQueryHandler handler(registry);

  const auto receipt = registry.register_async_accept(make_request(), make_async_result());
  assert_true(receipt.has_value(), "accepted async must register a receipt");

  const auto mismatch_query = handler.handle_query(
      receipt->receipt_id, "user://tenant-b/bob", receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::OwnerMismatch),
               static_cast<int>(mismatch_query.status),
               "non-owner query should be rejected as owner mismatch");

  const auto mismatch_cancel = handler.handle_cancel(
      receipt->receipt_id, "user://tenant-b/bob", receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::OwnerMismatch),
               static_cast<int>(mismatch_cancel.status),
               "non-owner cancel should be rejected as owner mismatch");

  // owner 仍应可查询 pending，证明 cancel 未被误执行
  const auto owner_query = handler.handle_query(
      receipt->receipt_id, receipt->actor_ref, receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::Ok),
               static_cast<int>(owner_query.status),
               "owner query should still succeed after rejected non-owner cancel");
  assert_equal(std::string("pending"), owner_query.task_status,
               "receipt should remain pending after rejected cancel");
}

void owner_cancel_marks_receipt_cancelled() {
  using dasall::access::AsyncTaskRegistry;
  using dasall::access::gateway::QueryHandlerResult;
  using dasall::access::gateway::TaskQueryHandler;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AsyncTaskRegistry registry("static-secret-027");
  TaskQueryHandler handler(registry);

  const auto receipt = registry.register_async_accept(make_request(), make_async_result());
  assert_true(receipt.has_value(), "accepted async must register a receipt");

  const auto cancelled = handler.handle_cancel(
      receipt->receipt_id, receipt->actor_ref, receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::Cancelled),
               static_cast<int>(cancelled.status),
               "owner cancel should return Cancelled");
  assert_equal(std::string("cancelled"), cancelled.task_status,
               "cancel result should carry cancelled status");

  const auto query_after_cancel = handler.handle_query(
      receipt->receipt_id, receipt->actor_ref, receipt->ownership_token);
  assert_equal(static_cast<int>(QueryHandlerResult::Status::Cancelled),
               static_cast<int>(query_after_cancel.status),
               "owner query should return Cancelled after cancel");
  assert_equal(std::string("cancelled"), query_after_cancel.task_status,
               "query should observe cancelled status");
}

}  // namespace

int main() {
  try {
    query_owner_receipt_returns_pending_completed_expired();
    non_owner_query_and_cancel_are_rejected();
    owner_cancel_marks_receipt_cancelled();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

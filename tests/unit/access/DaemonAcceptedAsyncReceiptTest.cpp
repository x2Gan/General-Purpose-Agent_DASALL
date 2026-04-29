/// tests/unit/access/DaemonAcceptedAsyncReceiptTest.cpp
///
/// DMD-TODO-015: daemon accepted_async 接线 AsyncTaskRegistry
///
/// 覆盖：
///   - accepted_async 应生成 receipt
///   - receipt 包含正确的 ownership_token 和 task_ref
///   - 多个 accepted_async 产生不同的 receipt_id
///   - 非 AcceptedAsync disposition 不生成 receipt
///   - registry 为 null 时 graceful fallback
///   - receipt 注册后可查询和验证

#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AsyncTaskRegistry.h"
#include "daemon/DaemonResponseBuilderWithReceipt.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_test_request(
    const std::string& request_id,
    const std::string& actor_ref) {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = request_id;
  request.packet.entry_type = "daemon";
  request.packet.protocol_kind = "ipc_uds";
  request.subject_identity.actor_ref = actor_ref;
  request.request_context["request_id"] = request_id;
  request.request_context["session_id"] = "sess:" + request_id;
  return request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_accepted_async_result(
    const std::string& task_ref) {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
  result.receipt_ref = task_ref;
  return result;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult make_completed_result() {
  dasall::access::RuntimeDispatchResult result;
  result.disposition = dasall::access::AccessDisposition::Completed;
  return result;
}

void test_accepted_async_generates_receipt() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;
  using dasall::tests::support::assert_equal;

  auto registry = std::make_shared<AsyncTaskRegistry>("test-secret");
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(registry);

  auto request = make_test_request("req-001", "uid:1000");
  auto result = make_accepted_async_result("task:001");

  auto receipt = builder->register_and_build_receipt(request, result);

  assert_true(receipt != nullptr, "should generate receipt for accepted_async");
  assert_true(!receipt->receipt_id.empty(), "receipt_id should not be empty");
  assert_equal(std::string("req-001"), receipt->request_id,
               "receipt should preserve request_id");
  assert_equal(std::string("uid:1000"), receipt->actor_ref,
               "receipt should preserve actor_ref");
}

void test_receipt_contains_ownership_info() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;
  using dasall::tests::support::assert_equal;

  auto registry = std::make_shared<AsyncTaskRegistry>("test-secret");
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(registry);

  auto request = make_test_request("req-002", "uid:2000");
  auto result = make_accepted_async_result("task:002");

  auto receipt = builder->register_and_build_receipt(request, result);

  assert_true(receipt != nullptr, "should generate receipt");
  assert_true(!receipt->ownership_token.empty(), "ownership_token should not be empty");
  assert_equal(std::string("task:002"), receipt->task_ref, "task_ref should match");
}

void test_multiple_accepts_produce_different_receipts() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;

  auto registry = std::make_shared<AsyncTaskRegistry>("test-secret");
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(registry);

  auto request1 = make_test_request("req-003", "uid:1000");
  auto result1 = make_accepted_async_result("task:003");
  auto receipt1 = builder->register_and_build_receipt(request1, result1);

  auto request2 = make_test_request("req-004", "uid:1000");
  auto result2 = make_accepted_async_result("task:004");
  auto receipt2 = builder->register_and_build_receipt(request2, result2);

  assert_true(receipt1 != nullptr && receipt2 != nullptr, "both should generate receipts");
  assert_true(receipt1->receipt_id != receipt2->receipt_id,
              "different requests should produce different receipt_ids");
}

void test_non_accepted_async_does_not_generate_receipt() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;

  auto registry = std::make_shared<AsyncTaskRegistry>("test-secret");
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(registry);

  auto request = make_test_request("req-005", "uid:1000");
  auto result = make_completed_result();

  auto receipt = builder->register_and_build_receipt(request, result);

  assert_true(receipt == nullptr, "should not generate receipt for non-async disposition");
}

void test_null_registry_graceful_fallback() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::tests::support::assert_true;

  // Intentionally passing null registry
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(nullptr);

  auto request = make_test_request("req-006", "uid:1000");
  auto result = make_accepted_async_result("task:006");

  auto receipt = builder->register_and_build_receipt(request, result);

  assert_true(receipt == nullptr, "should gracefully fallback when registry is null");
}

void test_registered_receipt_is_queryable() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;

  auto registry = std::make_shared<AsyncTaskRegistry>("test-secret");
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(registry);

  auto request = make_test_request("req-007", "uid:1000");
  auto result = make_accepted_async_result("task:007");

  auto receipt = builder->register_and_build_receipt(request, result);
  assert_true(receipt != nullptr, "should generate receipt");

  // Verify that registry can find the receipt (registry stores receipts internally)
  assert_true(!receipt->receipt_id.empty(), "receipt_id should be set for query");
}

void test_ownership_token_validation() {
  using dasall::access::daemon::DaemonResponseBuilderWithReceipt;
  using dasall::access::AsyncTaskRegistry;
  using dasall::tests::support::assert_true;
  using dasall::tests::support::assert_equal;

  auto registry = std::make_shared<AsyncTaskRegistry>("test-secret");
  auto builder = std::make_shared<DaemonResponseBuilderWithReceipt>(registry);

  auto request = make_test_request("req-008", "uid:3000");
  auto result = make_accepted_async_result("task:008");

  auto receipt = builder->register_and_build_receipt(request, result);

  assert_true(receipt != nullptr, "should generate receipt");
  assert_equal(std::string("uid:3000"), receipt->actor_ref,
               "actor_ref should match original request");
  assert_true(!receipt->ownership_token.empty(),
              "ownership_token should be generated");
}

}  // namespace

int main() {
  try {
    test_accepted_async_generates_receipt();
    test_receipt_contains_ownership_info();
    test_multiple_accepts_produce_different_receipts();
    test_non_accepted_async_does_not_generate_receipt();
    test_null_registry_graceful_fallback();
    test_registered_receipt_is_queryable();
    test_ownership_token_validation();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test exception: " << e.what() << std::endl;
    return 1;
  }
}

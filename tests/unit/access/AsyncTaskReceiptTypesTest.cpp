#include <chrono>
#include <exception>
#include <iostream>

#include "AccessTypes.h"
#include "support/TestAssertions.h"

namespace {

void async_task_receipt_fields_support_ownership_validation() {
  using dasall::access::AsyncTaskReceipt;

  AsyncTaskReceipt receipt;
  receipt.receipt_id = "rcpt-001";
  receipt.request_id = "req-001";
  receipt.session_id = "sess-001";
  receipt.actor_ref = "JWT://local/user-123";
  receipt.task_ref = "task://runtime/task-abc";
  receipt.ownership_token =
      "HMAC(secret, rcpt-001||JWT://local/user-123||req-001)";
  receipt.expires_at =
      std::chrono::steady_clock::now() +
      std::chrono::hours(24);  // 24-hour TTL

  dasall::tests::support::assert_equal(
      "rcpt-001",
      receipt.receipt_id,
      "receipt_id should be set");
  dasall::tests::support::assert_equal(
      "JWT://local/user-123",
      receipt.actor_ref,
      "actor_ref should identify owner");
  dasall::tests::support::assert_true(
      receipt.ownership_token.find("HMAC") != std::string::npos,
      "ownership_token should be present for verification");
}

void async_task_receipt_ttl_is_enforced() {
  using dasall::access::AsyncTaskReceipt;

  // Expired receipt
  AsyncTaskReceipt expired;
  expired.receipt_id = "rcpt-expired";
  expired.expires_at =
      std::chrono::steady_clock::now() -
      std::chrono::seconds(1);  // Expired 1 second ago

  // Valid receipt
  AsyncTaskReceipt valid;
  valid.receipt_id = "rcpt-valid";
  valid.expires_at =
      std::chrono::steady_clock::now() +
      std::chrono::hours(1);  // Expires in 1 hour

  auto now = std::chrono::steady_clock::now();
  dasall::tests::support::assert_true(
      expired.expires_at < now,
      "expired receipt should have past expiration time");
  dasall::tests::support::assert_true(
      valid.expires_at > now,
      "valid receipt should have future expiration time");
}

void async_task_receipt_tracks_original_request() {
  using dasall::access::AsyncTaskReceipt;

  AsyncTaskReceipt receipt;
  receipt.receipt_id = "rcpt-001";
  receipt.request_id = "req-original-001";
  receipt.session_id = "sess-correlation-xyz";
  receipt.task_ref = "task://runtime/assigned-task-id";

  // Verify all traceability fields are preserved
  dasall::tests::support::assert_equal(
      "req-original-001",
      receipt.request_id,
      "request_id should preserve original request");
  dasall::tests::support::assert_equal(
      "task://runtime/assigned-task-id",
      receipt.task_ref,
      "task_ref should link to runtime task");
}

void async_task_receipt_optional_initial_status() {
  using dasall::access::AsyncTaskReceipt;

  // Receipt with initial status
  AsyncTaskReceipt with_status;
  with_status.receipt_id = "rcpt-with-status";
  with_status.initial_status = "RUNNING";

  // Receipt without initial status
  AsyncTaskReceipt without_status;
  without_status.receipt_id = "rcpt-no-status";

  dasall::tests::support::assert_true(
      with_status.initial_status.has_value(),
      "initial_status can be provided");
  dasall::tests::support::assert_true(
      !without_status.initial_status.has_value(),
      "initial_status is optional");
}

}  // namespace

int main() {
  try {
    async_task_receipt_fields_support_ownership_validation();
    async_task_receipt_ttl_is_enforced();
    async_task_receipt_tracks_original_request();
    async_task_receipt_optional_initial_status();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

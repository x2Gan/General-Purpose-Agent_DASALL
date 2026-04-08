#include <exception>
#include <iostream>

#include "support/TestAssertions.h"
#include "linux/BlockingQueueProvider.h"

namespace {

void test_blocking_queue_provider_reports_overflow_and_timeout_paths() {
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::QueueItem;
  using dasall::platform::QueueOptions;
  using dasall::platform::linux::BlockingQueueProvider;
  using dasall::tests::support::assert_true;

  BlockingQueueProvider provider;
  QueueOptions options;
  options.capacity = 1;

  const auto created = provider.create_queue(options);
  assert_true(created.ok(), "create_queue should succeed with valid capacity");

  const QueueItem item{1U};
  const auto first_push = provider.push(*created.value, item, 1);
  assert_true(first_push.ok(), "first push should succeed when queue has space");

  const auto overflow_push = provider.push(*created.value, item, 0);
  assert_true(!overflow_push.ok(), "push should fail when queue is full and timeout is zero");
  assert_true(overflow_push.error->code == PlatformErrorCode::ResourceExhausted,
              "overflow path should map to ResourceExhausted");

  const auto timeout_pop = provider.pop(*created.value, 0);
  assert_true(timeout_pop.ok(), "pop should succeed after draining setup path below");
  assert_true(timeout_pop.value->has_item, "pop should return the pending item");

  const auto empty_timeout_pop = provider.pop(*created.value, 0);
  assert_true(!empty_timeout_pop.ok(), "pop should fail with timeout when queue is empty");
  assert_true(empty_timeout_pop.error->code == PlatformErrorCode::Timeout,
              "empty pop timeout should map to Timeout");
}

void test_blocking_queue_provider_reports_queue_closed_and_close_idempotence() {
  using dasall::platform::PlatformErrorCode;
  using dasall::platform::QueueItem;
  using dasall::platform::QueueOptions;
  using dasall::platform::QueueShutdownPolicy;
  using dasall::platform::linux::BlockingQueueProvider;
  using dasall::tests::support::assert_true;

  BlockingQueueProvider provider;
  QueueOptions options;
  options.capacity = 4;
  options.shutdown_policy = QueueShutdownPolicy::DropPending;

  const auto created = provider.create_queue(options);
  assert_true(created.ok(), "create_queue should succeed for close path setup");

  const QueueItem item{7U, 8U};
  assert_true(provider.push(*created.value, item, 1).ok(),
              "push should succeed before closing queue");

  const auto first_close = provider.close(*created.value);
  assert_true(first_close.ok(), "first close should succeed");
  assert_true(!first_close.value->already_closed,
              "first close should report already_closed=false");
  assert_true(first_close.value->dropped_items == 1,
              "drop pending policy should report dropped item count");

  const auto closed_push = provider.push(*created.value, item, 1);
  assert_true(!closed_push.ok(), "push should fail after queue is closed");
  assert_true(closed_push.error->code == PlatformErrorCode::QueueClosed,
              "push after close should map to QueueClosed");

  const auto second_close = provider.close(*created.value);
  assert_true(second_close.ok(), "second close should be idempotent success");
  assert_true(second_close.value->already_closed,
              "second close should report already_closed=true");
}

}  // namespace

int main() {
  try {
    test_blocking_queue_provider_reports_overflow_and_timeout_paths();
    test_blocking_queue_provider_reports_queue_closed_and_close_idempotence();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
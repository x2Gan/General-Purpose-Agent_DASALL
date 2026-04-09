#include <exception>
#include <iostream>

#include "execution/ExecutionSubscriptionHub.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::services::CapabilityTargetRef;
using dasall::services::ExecutionSubscriptionRequest;
using dasall::services::ServiceCallContext;
using dasall::services::internal::ExecutionSubscriptionHub;
using dasall::services::internal::ExecutionSubscriptionHubDependencies;

[[nodiscard]] ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 3000;

  return ServiceCallContext{
      .request_id = "req-018",
      .session_id = "session-018",
      .trace_id = "trace-018",
      .tool_call_id = "tool-018",
      .goal_id = "goal-018",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

[[nodiscard]] CapabilityTargetRef make_target() {
  return CapabilityTargetRef{
      .capability_id = "cap.exec",
      .target_id = "target-018",
  };
}

[[nodiscard]] ExecutionSubscriptionRequest make_request(std::optional<std::string> cursor = std::nullopt,
                                                        std::uint32_t max_events = 2U) {
  return ExecutionSubscriptionRequest{
      .context = make_context(),
      .target = make_target(),
      .stream_kind = "status",
      .cursor = std::move(cursor),
      .max_events = max_events,
  };
}

void test_execution_subscription_hub_returns_cursor_batch_without_leaking_buffer_details() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ExecutionSubscriptionHub hub(ExecutionSubscriptionHubDependencies{.max_buffered_events = 4U});
  hub.publish(make_target(), "status", {"{\"seq\":1}", "{\"seq\":2}"});

  const auto result = hub.subscribe(make_context(), make_request());

  assert_true(!result.error.has_value(), "normal subscribe should not surface an error");
  assert_equal(std::string("[{\"seq\":1},{\"seq\":2}]"),
               result.events_json,
               "subscribe should return event batch JSON only");
  assert_true(result.next_cursor.has_value(), "subscribe should return next_cursor");
  assert_equal(std::string("2"),
               *result.next_cursor,
               "next_cursor should advance to the last returned sequence");
  assert_true(!result.resync_required, "non-overflow batch should not require resync");
}

void test_execution_subscription_hub_sets_resync_required_after_drop_oldest_overflow() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ExecutionSubscriptionHub hub(ExecutionSubscriptionHubDependencies{.max_buffered_events = 2U});
  hub.publish(make_target(), "status", {"{\"seq\":1}"});
  hub.publish(make_target(), "status", {"{\"seq\":2}"});
  hub.publish(make_target(), "status", {"{\"seq\":3}"});

  const auto result = hub.subscribe(make_context(), make_request(std::string("0"), 2U));

  assert_true(result.error.has_value(), "overflow should surface structured runtime error");
  assert_true(result.error->failure_type.has_value(),
              "overflow should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Runtime),
               static_cast<int>(*result.error->failure_type),
               "overflow should map to runtime failure type");
  assert_true(result.resync_required, "drop_oldest overflow must force resync_required");
  assert_equal(1,
               static_cast<int>(result.dropped_count),
               "overflow should expose dropped_count");
}

void test_execution_subscription_hub_rejects_invalid_cursor() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ExecutionSubscriptionHub hub(ExecutionSubscriptionHubDependencies{.max_buffered_events = 4U});
  hub.publish(make_target(), "status", {"{\"seq\":1}"});

  const auto result = hub.subscribe(make_context(), make_request(std::string("cursor-x"), 2U));

  assert_true(result.error.has_value(), "invalid cursor should surface structured validation error");
  assert_true(result.error->failure_type.has_value(),
              "invalid cursor should populate failure_type");
  assert_equal(static_cast<int>(ResultCodeCategory::Validation),
               static_cast<int>(*result.error->failure_type),
               "invalid cursor should map to validation failure type");
}

}  // namespace

int main() {
  try {
    test_execution_subscription_hub_returns_cursor_batch_without_leaking_buffer_details();
    test_execution_subscription_hub_sets_resync_required_after_drop_oldest_overflow();
    test_execution_subscription_hub_rejects_invalid_cursor();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
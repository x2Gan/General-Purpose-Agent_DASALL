#include <exception>
#include <iostream>

#include "ServiceContextBuilder.h"
#include "support/TestAssertions.h"

namespace {

dasall::services::ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 9000;
  budget.max_turns = 5;

  return dasall::services::ServiceCallContext{
      .request_id = "req-009",
      .session_id = "session-009",
      .trace_id = "trace-009",
      .tool_call_id = "tool-call-009",
      .goal_id = "goal-009",
      .budget_guard = budget,
      .deadline_ms = 42000,
  };
}

void test_normalize_context_preserves_required_metadata() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const dasall::services::internal::ServiceContextBuilder builder;
  const auto result = builder.normalize_context(make_context());

  assert_true(result.ok(), "normalize_context should accept complete context metadata");
  assert_equal(std::string("req-009"), result.context->request_id,
               "request_id should be preserved");
  assert_equal(std::string("session-009"), result.context->session_id,
               "session_id should be preserved");
  assert_equal(std::string("trace-009"), result.context->trace_id,
               "trace_id should be preserved");
  assert_equal(std::string("tool-call-009"), result.context->tool_call_id,
               "tool_call_id should be preserved");
  assert_equal(std::string("goal-009"), result.context->goal_id,
               "goal_id should be preserved");
  assert_true(result.context->budget_guard.has_value(),
              "budget_guard should be preserved when provided");
  assert_true(result.context->budget_guard->max_latency_ms.has_value(),
              "budget max_latency_ms should stay present");
  assert_equal(9000, static_cast<int>(*result.context->budget_guard->max_latency_ms),
               "budget max_latency_ms should be preserved");
  assert_equal(42000, static_cast<int>(result.context->deadline_ms),
               "deadline_ms should be preserved");
}

void test_normalize_context_rejects_missing_request_id() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto invalid_context = make_context();
  invalid_context.request_id.clear();

  const dasall::services::internal::ServiceContextBuilder builder;
  const auto result = builder.normalize_context(invalid_context);

  assert_true(!result.ok(), "normalize_context should reject missing request_id");
  assert_equal(std::string("request_id is required"), result.error,
               "missing request_id should surface an observable error");
}

}  // namespace

int main() {
  try {
    test_normalize_context_preserves_required_metadata();
    test_normalize_context_rejects_missing_request_id();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
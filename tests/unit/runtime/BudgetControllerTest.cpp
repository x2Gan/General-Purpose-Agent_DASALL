#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "budget/BudgetController.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] const dasall::contracts::BudgetSnapshotEntry* find_entry(
    const dasall::contracts::BudgetSnapshot& snapshot,
    const dasall::contracts::BudgetType budget_type) {
  for (const auto& entry : snapshot.entries) {
    if (entry.budget_type == budget_type) {
      return &entry;
    }
  }

  return nullptr;
}

}  // namespace

int main() {
  using dasall::contracts::BudgetType;
  using dasall::runtime::BudgetController;
  using dasall::runtime::BudgetViolationClass;
  using dasall::runtime::RuntimeErrorCode;
  using dasall::runtime::budget_violation_error_code;
  using dasall::runtime::budget_violation_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    assert_equal("ToolCallExhausted",
                 std::string(budget_violation_name(BudgetViolationClass::ToolCallExhausted)),
                 "tool-call violation name should remain stable");
    assert_true(budget_violation_error_code(BudgetViolationClass::LatencyExhausted) ==
                    RuntimeErrorCode::RT_E_303_LATENCY_OVERRUN,
                "latency violation should map to RT_E_303");

    const dasall::runtime::BudgetInitializeRequest init_request{
        .runtime_budget = {.max_tokens = 1000,
                           .max_turns = 4,
                           .max_tool_calls = 1,
                           .max_latency_ms = 1500,
                           .max_replan_count = 2},
        .started_at_ms = 42,
    };

    BudgetController controller;
    const auto init_decision = controller.initialize(init_request);
    assert_true(init_decision.allowed, "valid runtime budget should initialize successfully");

    const auto snapshot = controller.snapshot();
    assert_true(snapshot.snapshot_at_ms == 42, "snapshot timestamp should reflect initialize request");
    assert_equal(5, static_cast<int>(snapshot.entries.size()),
                 "budget snapshot must expose exactly five budget dimensions");
    const auto* token_entry = find_entry(snapshot, BudgetType::Token);
    assert_true(token_entry != nullptr, "snapshot must include the token dimension");
    assert_true(token_entry->max == 1000, "token max should be copied from RuntimeBudget");

    assert_true(controller.can_continue().allowed,
                "freshly initialized controller should allow continuing the main loop");
    assert_true(controller.can_replan().allowed,
                "freshly initialized controller should allow replanning while budget remains");
    assert_true(controller.can_call_tool().allowed,
                "freshly initialized controller should allow one tool call");

    const auto consume_allowed = controller.consume(
        {.budget_type = BudgetType::ToolCall, .amount = 1, .observed_at_ms = 50, .detail = "first tool call"});
    assert_true(consume_allowed.allowed,
                "consuming the last available tool call should still be accepted");
    assert_true(controller.can_call_tool().allowed,
                "controller should permit use up to the declared max_tool_calls boundary");

    const auto consume_rejected = controller.consume(
        {.budget_type = BudgetType::ToolCall,
         .amount = 1,
         .observed_at_ms = 60,
         .detail = "tool-call budget exhausted"});
    assert_true(!consume_rejected.allowed, "exceeding max_tool_calls must be rejected");
    assert_true(consume_rejected.violation == BudgetViolationClass::ToolCallExhausted,
                "tool-call overflow should report ToolCallExhausted");
    assert_true(consume_rejected.error_code == RuntimeErrorCode::RT_E_302_TOOL_CALL_OVERRUN,
                "tool-call overflow should map to RT_E_302");
    assert_true(!controller.can_call_tool().allowed,
                "post-overflow controller must reject further tool calls");

    BudgetController replan_controller;
    assert_true(replan_controller.initialize(init_request).allowed,
                "replan controller should initialize with the same valid budget");
    assert_true(replan_controller.consume(
                    {.budget_type = BudgetType::Replan,
                     .amount = 2,
                     .observed_at_ms = 70,
                     .detail = "consume replan budget to boundary"})
                    .allowed,
                "consuming up to the replan boundary should remain allowed");
    const auto replan_reject = replan_controller.consume(
        {.budget_type = BudgetType::Replan,
         .amount = 1,
         .observed_at_ms = 80,
         .detail = "replan budget exhausted"});
    assert_true(!replan_reject.allowed, "replan overflow must be rejected");
    assert_true(replan_reject.violation == BudgetViolationClass::ReplanExhausted,
                "replan overflow should report ReplanExhausted");
    assert_true(replan_controller.can_replan().error_code == RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN,
                "replan overflow should map to RT_E_304");

    BudgetController latency_controller;
    assert_true(latency_controller.initialize(init_request).allowed,
                "latency controller should initialize successfully");
    const auto latency_allowed = latency_controller.consume(
        {.budget_type = BudgetType::Latency,
         .amount = 1,
         .observed_at_ms = 1000,
         .detail = "latency within limit"});
    assert_true(latency_allowed.allowed, "latency within max_latency_ms should remain allowed");
    const auto latency_snapshot = latency_controller.snapshot();
    const auto* latency_entry = find_entry(latency_snapshot, BudgetType::Latency);
    assert_true(latency_entry != nullptr, "snapshot must include the latency dimension");
    assert_true(latency_entry->current == 958,
                "latency current should reflect observed_at_ms - started_at_ms");
    const auto latency_reject = latency_controller.consume(
        {.budget_type = BudgetType::Latency,
         .amount = 999,
         .observed_at_ms = 2000,
         .detail = "latency budget exhausted"});
    assert_true(!latency_reject.allowed, "latency overflow must be rejected");
    assert_true(latency_reject.violation == BudgetViolationClass::LatencyExhausted,
                "latency overflow should report LatencyExhausted");
    assert_true(!latency_controller.can_continue().allowed,
                "latency exhaustion should block can_continue");

    BudgetController token_controller;
    assert_true(token_controller.initialize(init_request).allowed,
                "token controller should initialize successfully");
    assert_true(token_controller.consume(
                    {.budget_type = BudgetType::Token,
                     .amount = 1000,
                     .observed_at_ms = 90,
                     .detail = "consume token budget to boundary"})
                    .allowed,
                "token usage up to the boundary should remain allowed");
    const auto token_reject = token_controller.consume(
        {.budget_type = BudgetType::Token,
         .amount = 1,
         .observed_at_ms = 91,
         .detail = "token budget exhausted"});
    assert_true(!token_reject.allowed, "token overflow must be rejected");
    assert_true(token_reject.violation == BudgetViolationClass::TokenExhausted,
                "token overflow should report TokenExhausted");
    assert_true(!token_controller.can_continue().allowed,
                "token exhaustion should block can_continue");

    BudgetController invalid_controller;
    const auto invalid_init = invalid_controller.initialize(
        {.runtime_budget = {.max_tokens = 1000,
                            .max_turns = std::nullopt,
                            .max_tool_calls = 1,
                            .max_latency_ms = 1500,
                            .max_replan_count = 2},
         .started_at_ms = 42});
    assert_true(!invalid_init.allowed, "missing required budget dimension must reject initialize");
    assert_true(invalid_init.violation == BudgetViolationClass::ConfigurationInvalid,
                "invalid initialize should report ConfigurationInvalid");
    assert_true(invalid_init.error_code == RuntimeErrorCode::RT_E_100_CONFIG_MISSING,
                "invalid initialize should map to RT_E_100");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "budget/IBudgetController.h"
#include "checkpoint/RuntimeBudgetGuards.h"
#include "support/TestAssertions.h"

namespace {

dasall::runtime::BudgetViolationClass exhausted_violation_for(
    const dasall::contracts::BudgetType budget_type) {
  switch (budget_type) {
    case dasall::contracts::BudgetType::Token:
      return dasall::runtime::BudgetViolationClass::TokenExhausted;
    case dasall::contracts::BudgetType::Turn:
      return dasall::runtime::BudgetViolationClass::TurnExhausted;
    case dasall::contracts::BudgetType::ToolCall:
      return dasall::runtime::BudgetViolationClass::ToolCallExhausted;
    case dasall::contracts::BudgetType::Latency:
      return dasall::runtime::BudgetViolationClass::LatencyExhausted;
    case dasall::contracts::BudgetType::Replan:
      return dasall::runtime::BudgetViolationClass::ReplanExhausted;
  }

  return dasall::runtime::BudgetViolationClass::SnapshotUnavailable;
}

class FakeBudgetController final : public dasall::runtime::IBudgetController {
 public:
  [[nodiscard]] dasall::runtime::BudgetDecision initialize(
      const dasall::runtime::BudgetInitializeRequest& request) override {
    const auto guard_result = dasall::contracts::validate_runtime_budget(request.runtime_budget);
    if (!guard_result.ok) {
      initialized_ = false;
      return dasall::runtime::make_budget_rejected_decision(
          dasall::runtime::BudgetViolationClass::ConfigurationInvalid,
          std::string(guard_result.reason));
    }

    budget_ = request.runtime_budget;
    snapshot_ = {
        .snapshot_at_ms = request.started_at_ms,
        .entries = {
            {.budget_type = dasall::contracts::BudgetType::Token,
             .current = 0,
             .max = *budget_.max_tokens,
             .remaining = static_cast<std::int64_t>(*budget_.max_tokens),
             .reject_reason = std::nullopt},
            {.budget_type = dasall::contracts::BudgetType::Turn,
             .current = 0,
             .max = *budget_.max_turns,
             .remaining = static_cast<std::int64_t>(*budget_.max_turns),
             .reject_reason = std::nullopt},
            {.budget_type = dasall::contracts::BudgetType::ToolCall,
             .current = 0,
             .max = *budget_.max_tool_calls,
             .remaining = static_cast<std::int64_t>(*budget_.max_tool_calls),
             .reject_reason = std::nullopt},
            {.budget_type = dasall::contracts::BudgetType::Latency,
             .current = 0,
             .max = *budget_.max_latency_ms,
             .remaining = static_cast<std::int64_t>(*budget_.max_latency_ms),
             .reject_reason = std::nullopt},
            {.budget_type = dasall::contracts::BudgetType::Replan,
             .current = 0,
             .max = *budget_.max_replan_count,
             .remaining = static_cast<std::int64_t>(*budget_.max_replan_count),
             .reject_reason = std::nullopt},
        },
        .overall_reject_reason = std::nullopt,
    };
    initialized_ = true;
    return dasall::runtime::make_budget_allowed_decision(std::nullopt, "budget initialized");
  }

  [[nodiscard]] dasall::runtime::BudgetDecision consume(
      const dasall::runtime::BudgetConsumeRequest& request) override {
    if (!initialized_) {
      return dasall::runtime::make_budget_rejected_decision(
          dasall::runtime::BudgetViolationClass::SnapshotUnavailable,
          "budget controller must be initialized before consume");
    }

    auto* entry = find_entry(request.budget_type);
    entry->current += request.amount;
    entry->remaining = static_cast<std::int64_t>(entry->max) -
                       static_cast<std::int64_t>(entry->current);
    snapshot_.snapshot_at_ms = request.observed_at_ms;

    if (entry->current > entry->max) {
      entry->reject_reason = request.detail.empty() ? std::optional<std::string>("budget exhausted")
                                                    : std::optional<std::string>(request.detail);
      snapshot_.overall_reject_reason = entry->reject_reason;
      return dasall::runtime::make_budget_rejected_decision(
          exhausted_violation_for(request.budget_type),
          *entry->reject_reason,
          request.budget_type);
    }

    entry->reject_reason = std::nullopt;
    snapshot_.overall_reject_reason = std::nullopt;
    return dasall::runtime::make_budget_allowed_decision(request.budget_type,
                                                         request.detail.empty() ? "budget consumed"
                                                                                : request.detail);
  }

  [[nodiscard]] dasall::contracts::BudgetSnapshot snapshot() const override {
    return snapshot_;
  }

  [[nodiscard]] dasall::runtime::BudgetDecision can_continue() const override {
    return decision_for(dasall::contracts::BudgetType::Turn, "turn budget available");
  }

  [[nodiscard]] dasall::runtime::BudgetDecision can_replan() const override {
    return decision_for(dasall::contracts::BudgetType::Replan, "replan budget available");
  }

  [[nodiscard]] dasall::runtime::BudgetDecision can_call_tool() const override {
    return decision_for(dasall::contracts::BudgetType::ToolCall, "tool-call budget available");
  }

 private:
  dasall::contracts::BudgetSnapshotEntry* find_entry(const dasall::contracts::BudgetType budget_type) {
    for (auto& entry : snapshot_.entries) {
      if (entry.budget_type == budget_type) {
        return &entry;
      }
    }

    return nullptr;
  }

  [[nodiscard]] dasall::runtime::BudgetDecision decision_for(
      const dasall::contracts::BudgetType budget_type,
      const std::string& allowed_detail) const {
    if (!initialized_) {
      return dasall::runtime::make_budget_rejected_decision(
          dasall::runtime::BudgetViolationClass::SnapshotUnavailable,
          "budget controller has no snapshot");
    }

    for (const auto& entry : snapshot_.entries) {
      if (entry.budget_type != budget_type) {
        continue;
      }

      if (entry.current > entry.max) {
        return dasall::runtime::make_budget_rejected_decision(
            exhausted_violation_for(budget_type),
            entry.reject_reason.value_or("budget exhausted"),
            budget_type);
      }

      return dasall::runtime::make_budget_allowed_decision(budget_type, allowed_detail);
    }

    return dasall::runtime::make_budget_rejected_decision(
        dasall::runtime::BudgetViolationClass::SnapshotUnavailable,
        "budget snapshot entry is missing",
        budget_type);
  }

  bool initialized_ = false;
  dasall::contracts::RuntimeBudget budget_{};
  dasall::contracts::BudgetSnapshot snapshot_{};
};

}  // namespace

int main() {
  using dasall::contracts::BudgetType;
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

    FakeBudgetController controller;
    const dasall::runtime::BudgetInitializeRequest init_request{
        .runtime_budget = {.max_tokens = 1000,
                           .max_turns = 4,
                           .max_tool_calls = 1,
                           .max_latency_ms = 1500,
                           .max_replan_count = 2},
        .started_at_ms = 42,
    };

    const auto init_decision = controller.initialize(init_request);
    assert_true(init_decision.allowed, "valid runtime budget should initialize successfully");

    const auto snapshot = controller.snapshot();
    assert_true(snapshot.snapshot_at_ms == 42, "snapshot timestamp should reflect initialize request");
    assert_equal(5, static_cast<int>(snapshot.entries.size()),
                 "budget snapshot must expose exactly five budget dimensions");
    assert_true(snapshot.entries[0].budget_type == BudgetType::Token,
                "first snapshot entry should remain the token dimension");
    assert_true(snapshot.entries[0].max == 1000, "token max should be copied from RuntimeBudget");

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
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
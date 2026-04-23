#include "BudgetController.h"

#include <optional>
#include <string>

#include "checkpoint/BudgetSnapshotGuards.h"
#include "checkpoint/RuntimeBudgetGuards.h"

namespace dasall::runtime {
namespace {

[[nodiscard]] BudgetViolationClass exhausted_violation_for(
    const contracts::BudgetType budget_type) {
  switch (budget_type) {
    case contracts::BudgetType::Token:
      return BudgetViolationClass::TokenExhausted;
    case contracts::BudgetType::Turn:
      return BudgetViolationClass::TurnExhausted;
    case contracts::BudgetType::ToolCall:
      return BudgetViolationClass::ToolCallExhausted;
    case contracts::BudgetType::Latency:
      return BudgetViolationClass::LatencyExhausted;
    case contracts::BudgetType::Replan:
      return BudgetViolationClass::ReplanExhausted;
  }

  return BudgetViolationClass::SnapshotUnavailable;
}

[[nodiscard]] contracts::BudgetSnapshot build_initial_snapshot(
    const contracts::RuntimeBudget& runtime_budget,
    const std::uint64_t started_at_ms) {
  return contracts::BudgetSnapshot{
      .snapshot_at_ms = started_at_ms,
      .entries = {
          {.budget_type = contracts::BudgetType::Token,
           .current = 0,
           .max = *runtime_budget.max_tokens,
           .remaining = static_cast<std::int64_t>(*runtime_budget.max_tokens),
           .reject_reason = std::nullopt},
          {.budget_type = contracts::BudgetType::Turn,
           .current = 0,
           .max = *runtime_budget.max_turns,
           .remaining = static_cast<std::int64_t>(*runtime_budget.max_turns),
           .reject_reason = std::nullopt},
          {.budget_type = contracts::BudgetType::ToolCall,
           .current = 0,
           .max = *runtime_budget.max_tool_calls,
           .remaining = static_cast<std::int64_t>(*runtime_budget.max_tool_calls),
           .reject_reason = std::nullopt},
          {.budget_type = contracts::BudgetType::Latency,
           .current = 0,
           .max = *runtime_budget.max_latency_ms,
           .remaining = static_cast<std::int64_t>(*runtime_budget.max_latency_ms),
           .reject_reason = std::nullopt},
          {.budget_type = contracts::BudgetType::Replan,
           .current = 0,
           .max = *runtime_budget.max_replan_count,
           .remaining = static_cast<std::int64_t>(*runtime_budget.max_replan_count),
           .reject_reason = std::nullopt},
      },
      .overall_reject_reason = std::nullopt,
  };
}

[[nodiscard]] std::optional<contracts::RuntimeBudget> runtime_budget_from_snapshot(
    const contracts::BudgetSnapshot& snapshot) {
  contracts::RuntimeBudget runtime_budget;
  bool has_token = false;
  bool has_turn = false;
  bool has_tool_call = false;
  bool has_latency = false;
  bool has_replan = false;

  for (const auto& entry : snapshot.entries) {
    switch (entry.budget_type) {
      case contracts::BudgetType::Token:
        runtime_budget.max_tokens = entry.max;
        has_token = true;
        break;
      case contracts::BudgetType::Turn:
        runtime_budget.max_turns = entry.max;
        has_turn = true;
        break;
      case contracts::BudgetType::ToolCall:
        runtime_budget.max_tool_calls = entry.max;
        has_tool_call = true;
        break;
      case contracts::BudgetType::Latency:
        runtime_budget.max_latency_ms = entry.max;
        has_latency = true;
        break;
      case contracts::BudgetType::Replan:
        runtime_budget.max_replan_count = entry.max;
        has_replan = true;
        break;
    }
  }

  if (!has_token || !has_turn || !has_tool_call || !has_latency || !has_replan) {
    return std::nullopt;
  }

  return runtime_budget;
}

}  // namespace

BudgetDecision BudgetController::initialize(const BudgetInitializeRequest& request) {
  const auto guard_result = contracts::validate_runtime_budget(request.runtime_budget);

  const std::lock_guard<std::mutex> lock(budget_mutex_);
  if (!guard_result.ok) {
    initialized_ = false;
    started_at_ms_ = 0;
    runtime_budget_ = {};
    snapshot_ = {};
    return make_budget_rejected_decision(
        BudgetViolationClass::ConfigurationInvalid,
        std::string(guard_result.reason));
  }

  initialized_ = true;
  started_at_ms_ = request.started_at_ms;
  runtime_budget_ = request.runtime_budget;
  snapshot_ = build_initial_snapshot(runtime_budget_, started_at_ms_);
  return make_budget_allowed_decision(std::nullopt, "budget initialized");
}

BudgetDecision BudgetController::restore(
    const contracts::BudgetSnapshot& snapshot,
    const std::uint64_t started_at_ms) {
  const auto snapshot_guard = contracts::validate_budget_snapshot(snapshot);
  if (!snapshot_guard.ok) {
    const std::lock_guard<std::mutex> lock(budget_mutex_);
    initialized_ = false;
    started_at_ms_ = 0;
    runtime_budget_ = {};
    snapshot_ = {};
    return make_budget_rejected_decision(
        BudgetViolationClass::SnapshotUnavailable,
        std::string(snapshot_guard.reason));
  }

  const auto runtime_budget = runtime_budget_from_snapshot(snapshot);
  if (!runtime_budget.has_value()) {
    const std::lock_guard<std::mutex> lock(budget_mutex_);
    initialized_ = false;
    started_at_ms_ = 0;
    runtime_budget_ = {};
    snapshot_ = {};
    return make_budget_rejected_decision(
        BudgetViolationClass::SnapshotUnavailable,
        "budget snapshot is missing one or more dimensions");
  }

  const auto guard_result = contracts::validate_runtime_budget(*runtime_budget);
  if (!guard_result.ok) {
    const std::lock_guard<std::mutex> lock(budget_mutex_);
    initialized_ = false;
    started_at_ms_ = 0;
    runtime_budget_ = {};
    snapshot_ = {};
    return make_budget_rejected_decision(
        BudgetViolationClass::ConfigurationInvalid,
        std::string(guard_result.reason));
  }

  const std::lock_guard<std::mutex> lock(budget_mutex_);
  initialized_ = true;
  started_at_ms_ = started_at_ms;
  runtime_budget_ = *runtime_budget;
  snapshot_ = snapshot;
  if (!snapshot_.snapshot_at_ms.has_value()) {
    snapshot_.snapshot_at_ms = started_at_ms_;
  }
  refresh_overall_reject_reason_locked();
  return make_budget_allowed_decision(std::nullopt, "budget restored from checkpoint snapshot");
}

BudgetDecision BudgetController::consume(const BudgetConsumeRequest& request) {
  const std::lock_guard<std::mutex> lock(budget_mutex_);
  if (!initialized_) {
    return make_budget_rejected_decision(
        BudgetViolationClass::SnapshotUnavailable,
        "budget controller must be initialized before consume");
  }

  auto* entry = find_entry_locked(request.budget_type);
  if (entry == nullptr) {
    return make_budget_rejected_decision(
        BudgetViolationClass::SnapshotUnavailable,
        "budget snapshot entry is missing",
        request.budget_type);
  }

  if (request.budget_type == contracts::BudgetType::Latency) {
    entry->current = request.observed_at_ms > started_at_ms_ ? request.observed_at_ms - started_at_ms_
                                                             : 0;
  } else {
    entry->current += request.amount;
  }

  entry->remaining = static_cast<std::int64_t>(entry->max) -
                     static_cast<std::int64_t>(entry->current);
  snapshot_.snapshot_at_ms = request.observed_at_ms;

  if (entry->current > entry->max) {
    entry->reject_reason = request.detail.empty() ? std::optional<std::string>("budget exhausted")
                                                  : std::optional<std::string>(request.detail);
    refresh_overall_reject_reason_locked();
    return make_budget_rejected_decision(
        exhausted_violation_for(request.budget_type),
        *entry->reject_reason,
        request.budget_type);
  }

  entry->reject_reason = std::nullopt;
  refresh_overall_reject_reason_locked();
  return make_budget_allowed_decision(
      request.budget_type,
      request.detail.empty() ? "budget consumed" : request.detail);
}

contracts::BudgetSnapshot BudgetController::snapshot() const {
  const std::lock_guard<std::mutex> lock(budget_mutex_);
  return snapshot_;
}

BudgetDecision BudgetController::can_continue() const {
  const std::lock_guard<std::mutex> lock(budget_mutex_);
  return decision_for_locked(
      {contracts::BudgetType::Token, contracts::BudgetType::Turn, contracts::BudgetType::Latency},
      contracts::BudgetType::Turn,
      "continue budget available");
}

BudgetDecision BudgetController::can_replan() const {
  const std::lock_guard<std::mutex> lock(budget_mutex_);
  return decision_for_locked(
      {contracts::BudgetType::Token,
       contracts::BudgetType::Turn,
       contracts::BudgetType::Latency,
       contracts::BudgetType::Replan},
      contracts::BudgetType::Replan,
      "replan budget available");
}

BudgetDecision BudgetController::can_call_tool() const {
  const std::lock_guard<std::mutex> lock(budget_mutex_);
  return decision_for_locked(
      {contracts::BudgetType::Token,
       contracts::BudgetType::Turn,
       contracts::BudgetType::Latency,
       contracts::BudgetType::ToolCall},
      contracts::BudgetType::ToolCall,
      "tool-call budget available");
}

contracts::BudgetSnapshotEntry* BudgetController::find_entry_locked(
    const contracts::BudgetType budget_type) {
  for (auto& entry : snapshot_.entries) {
    if (entry.budget_type == budget_type) {
      return &entry;
    }
  }

  return nullptr;
}

const contracts::BudgetSnapshotEntry* BudgetController::find_entry_locked(
    const contracts::BudgetType budget_type) const {
  for (const auto& entry : snapshot_.entries) {
    if (entry.budget_type == budget_type) {
      return &entry;
    }
  }

  return nullptr;
}

BudgetDecision BudgetController::decision_for_locked(
    const std::initializer_list<contracts::BudgetType> required_dimensions,
    const std::optional<contracts::BudgetType> allowed_budget_type,
    const std::string& allowed_detail) const {
  if (!initialized_) {
    return make_budget_rejected_decision(
        BudgetViolationClass::SnapshotUnavailable,
        "budget controller has no snapshot");
  }

  for (const auto budget_type : required_dimensions) {
    const auto* entry = find_entry_locked(budget_type);
    if (entry == nullptr) {
      return make_budget_rejected_decision(
          BudgetViolationClass::SnapshotUnavailable,
          "budget snapshot entry is missing",
          budget_type);
    }

    if (entry->current > entry->max) {
      return make_budget_rejected_decision(
          exhausted_violation_for(budget_type),
          entry->reject_reason.value_or("budget exhausted"),
          budget_type);
    }
  }

  return make_budget_allowed_decision(allowed_budget_type, allowed_detail);
}

void BudgetController::refresh_overall_reject_reason_locked() {
  snapshot_.overall_reject_reason = std::nullopt;
  for (const auto& entry : snapshot_.entries) {
    if (entry.reject_reason.has_value()) {
      snapshot_.overall_reject_reason = entry.reject_reason;
      return;
    }
  }
}

}  // namespace dasall::runtime
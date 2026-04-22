#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "checkpoint/BudgetSnapshot.h"
#include "checkpoint/RuntimeBudget.h"
#include "RuntimeErrorCode.h"

namespace dasall::runtime {

struct BudgetInitializeRequest {
  contracts::RuntimeBudget runtime_budget;
  std::uint64_t started_at_ms = 0;
};

struct BudgetConsumeRequest {
  contracts::BudgetType budget_type = contracts::BudgetType::Turn;
  std::uint64_t amount = 1;
  std::uint64_t observed_at_ms = 0;
  std::string detail;
};

enum class BudgetViolationClass : std::uint8_t {
  None = 0,
  ConfigurationInvalid,
  SnapshotUnavailable,
  TokenExhausted,
  TurnExhausted,
  ToolCallExhausted,
  LatencyExhausted,
  ReplanExhausted,
};

[[nodiscard]] constexpr const char* budget_violation_name(const BudgetViolationClass violation) {
  switch (violation) {
    case BudgetViolationClass::None:
      return "None";
    case BudgetViolationClass::ConfigurationInvalid:
      return "ConfigurationInvalid";
    case BudgetViolationClass::SnapshotUnavailable:
      return "SnapshotUnavailable";
    case BudgetViolationClass::TokenExhausted:
      return "TokenExhausted";
    case BudgetViolationClass::TurnExhausted:
      return "TurnExhausted";
    case BudgetViolationClass::ToolCallExhausted:
      return "ToolCallExhausted";
    case BudgetViolationClass::LatencyExhausted:
      return "LatencyExhausted";
    case BudgetViolationClass::ReplanExhausted:
      return "ReplanExhausted";
  }

  return "Unknown";
}

[[nodiscard]] constexpr std::optional<contracts::BudgetType> violated_budget_type(
    const BudgetViolationClass violation) {
  switch (violation) {
    case BudgetViolationClass::TokenExhausted:
      return contracts::BudgetType::Token;
    case BudgetViolationClass::TurnExhausted:
      return contracts::BudgetType::Turn;
    case BudgetViolationClass::ToolCallExhausted:
      return contracts::BudgetType::ToolCall;
    case BudgetViolationClass::LatencyExhausted:
      return contracts::BudgetType::Latency;
    case BudgetViolationClass::ReplanExhausted:
      return contracts::BudgetType::Replan;
    case BudgetViolationClass::None:
    case BudgetViolationClass::ConfigurationInvalid:
    case BudgetViolationClass::SnapshotUnavailable:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] constexpr std::optional<RuntimeErrorCode> budget_violation_error_code(
    const BudgetViolationClass violation) {
  switch (violation) {
    case BudgetViolationClass::ConfigurationInvalid:
      return RuntimeErrorCode::RT_E_100_CONFIG_MISSING;
    case BudgetViolationClass::SnapshotUnavailable:
      return RuntimeErrorCode::RT_E_202_STATE_INCONSISTENT;
    case BudgetViolationClass::TokenExhausted:
      return RuntimeErrorCode::RT_E_300_BUDGET_EXHAUSTED;
    case BudgetViolationClass::TurnExhausted:
      return RuntimeErrorCode::RT_E_301_TURN_OVERRUN;
    case BudgetViolationClass::ToolCallExhausted:
      return RuntimeErrorCode::RT_E_302_TOOL_CALL_OVERRUN;
    case BudgetViolationClass::LatencyExhausted:
      return RuntimeErrorCode::RT_E_303_LATENCY_OVERRUN;
    case BudgetViolationClass::ReplanExhausted:
      return RuntimeErrorCode::RT_E_304_REPLAN_OVERRUN;
    case BudgetViolationClass::None:
      return std::nullopt;
  }

  return std::nullopt;
}

struct BudgetDecision {
  bool allowed = false;
  std::optional<contracts::BudgetType> budget_type;
  BudgetViolationClass violation = BudgetViolationClass::None;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool rejected() const {
    return !allowed;
  }
};

[[nodiscard]] inline BudgetDecision make_budget_allowed_decision(
    const std::optional<contracts::BudgetType> budget_type,
    const std::string& detail = std::string()) {
  return BudgetDecision{
      .allowed = true,
      .budget_type = budget_type,
      .violation = BudgetViolationClass::None,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline BudgetDecision make_budget_rejected_decision(
    const BudgetViolationClass violation,
    const std::string& detail,
    const std::optional<contracts::BudgetType> budget_type = std::nullopt) {
  return BudgetDecision{
      .allowed = false,
      .budget_type = budget_type.has_value() ? budget_type : violated_budget_type(violation),
      .violation = violation,
      .error_code = budget_violation_error_code(violation),
      .detail = detail,
  };
}

}  // namespace dasall::runtime
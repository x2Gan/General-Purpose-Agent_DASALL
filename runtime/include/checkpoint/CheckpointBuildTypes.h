#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "RuntimeErrorCode.h"
#include "checkpoint/BudgetSnapshot.h"
#include "checkpoint/Checkpoint.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime {

inline constexpr std::string_view kRuntimeCheckpointSchemaVersionTag = "rt.schema_version";
inline constexpr std::string_view kRuntimeCheckpointFsmStateEnumVersionTag =
    "rt.fsm_state_enum_version";
inline constexpr std::string_view kRuntimeCheckpointBudgetSchemaVersionTag =
    "rt.budget_schema_version";

inline constexpr std::string_view kRuntimeCheckpointSchemaVersion = "1";
inline constexpr std::string_view kRuntimeCheckpointFsmStateEnumVersion = "1";
inline constexpr std::string_view kRuntimeCheckpointBudgetSchemaVersion = "1";

enum class CheckpointConsistencyIssue : std::uint8_t {
  None = 0,
  MissingCheckpointId,
  MissingStepId,
  MissingWorkingMemorySnapshot,
  TransitionRejected,
  InvalidCheckpointState,
  MissingPendingAction,
  MissingVersionTag,
  UnsupportedSchemaVersion,
  UnsupportedFsmStateVersion,
  UnsupportedBudgetSchemaVersion,
};

[[nodiscard]] constexpr const char* checkpoint_consistency_issue_name(
    const CheckpointConsistencyIssue issue) {
  switch (issue) {
    case CheckpointConsistencyIssue::None:
      return "None";
    case CheckpointConsistencyIssue::MissingCheckpointId:
      return "MissingCheckpointId";
    case CheckpointConsistencyIssue::MissingStepId:
      return "MissingStepId";
    case CheckpointConsistencyIssue::MissingWorkingMemorySnapshot:
      return "MissingWorkingMemorySnapshot";
    case CheckpointConsistencyIssue::TransitionRejected:
      return "TransitionRejected";
    case CheckpointConsistencyIssue::InvalidCheckpointState:
      return "InvalidCheckpointState";
    case CheckpointConsistencyIssue::MissingPendingAction:
      return "MissingPendingAction";
    case CheckpointConsistencyIssue::MissingVersionTag:
      return "MissingVersionTag";
    case CheckpointConsistencyIssue::UnsupportedSchemaVersion:
      return "UnsupportedSchemaVersion";
    case CheckpointConsistencyIssue::UnsupportedFsmStateVersion:
      return "UnsupportedFsmStateVersion";
    case CheckpointConsistencyIssue::UnsupportedBudgetSchemaVersion:
      return "UnsupportedBudgetSchemaVersion";
  }

  return "Unknown";
}

[[nodiscard]] constexpr std::optional<RuntimeErrorCode> checkpoint_consistency_error_code(
    const CheckpointConsistencyIssue issue) {
  switch (issue) {
    case CheckpointConsistencyIssue::None:
      return std::nullopt;
    case CheckpointConsistencyIssue::MissingCheckpointId:
    case CheckpointConsistencyIssue::MissingStepId:
    case CheckpointConsistencyIssue::MissingWorkingMemorySnapshot:
    case CheckpointConsistencyIssue::TransitionRejected:
    case CheckpointConsistencyIssue::InvalidCheckpointState:
    case CheckpointConsistencyIssue::MissingPendingAction:
    case CheckpointConsistencyIssue::MissingVersionTag:
      return RuntimeErrorCode::RT_E_202_STATE_INCONSISTENT;
    case CheckpointConsistencyIssue::UnsupportedSchemaVersion:
    case CheckpointConsistencyIssue::UnsupportedFsmStateVersion:
    case CheckpointConsistencyIssue::UnsupportedBudgetSchemaVersion:
      return RuntimeErrorCode::RT_E_412_RESUME_REJECTED;
  }

  return std::nullopt;
}

struct CheckpointBuildRequest {
  StateTransitionOutcome transition_outcome;
  std::string checkpoint_id;
  std::string step_id;
  std::string working_memory_snapshot;
  std::optional<std::string> pending_action;
  std::optional<std::string> request_id;
  std::optional<std::string> goal_id;
  std::optional<std::string> belief_state_ref;
  std::optional<std::uint32_t> retry_count;
  std::optional<std::int64_t> created_at_ms;
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  std::vector<std::string> tags;
};

struct CheckpointConsistencyReport {
  bool consistent = false;
  CheckpointConsistencyIssue issue = CheckpointConsistencyIssue::None;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool rejected() const {
    return !consistent;
  }
};

struct CheckpointBuildResult {
  std::optional<contracts::Checkpoint> checkpoint;
  CheckpointConsistencyReport report;

  [[nodiscard]] bool built() const {
    return checkpoint.has_value() && report.consistent;
  }
};

struct CheckpointPersistResult {
  bool persisted = false;
  std::optional<std::string> checkpoint_ref;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool failed() const {
    return !persisted;
  }
};

struct CheckpointLoadResult {
  std::optional<contracts::Checkpoint> checkpoint;
  CheckpointConsistencyReport report;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool loaded() const {
    return checkpoint.has_value() && report.consistent && !error_code.has_value();
  }
};

[[nodiscard]] inline CheckpointConsistencyReport make_checkpoint_consistent_report(
    const std::string& detail = std::string()) {
  return CheckpointConsistencyReport{
      .consistent = true,
      .issue = CheckpointConsistencyIssue::None,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline CheckpointConsistencyReport make_checkpoint_rejected_report(
    const CheckpointConsistencyIssue issue,
    const std::string& detail) {
  return CheckpointConsistencyReport{
      .consistent = false,
      .issue = issue,
      .error_code = checkpoint_consistency_error_code(issue),
      .detail = detail,
  };
}

[[nodiscard]] inline std::string make_checkpoint_tag(
    const std::string_view& key,
    const std::string_view& value) {
  return std::string(key) + "=" + std::string(value);
}

[[nodiscard]] inline std::optional<std::string> find_checkpoint_tag_value(
    const std::vector<std::string>& tags,
    const std::string_view& key) {
  const auto prefix = std::string(key) + "=";
  for (const auto& candidate : tags) {
    if (candidate.rfind(prefix, 0) == 0) {
      return candidate.substr(prefix.size());
    }
  }

  return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string> find_checkpoint_tag_value(
    const std::optional<std::vector<std::string>>& tags,
    const std::string_view& key) {
  if (!tags.has_value()) {
    return std::nullopt;
  }

  return find_checkpoint_tag_value(*tags, key);
}

[[nodiscard]] inline std::optional<std::string> find_checkpoint_tag_value(
    const contracts::Checkpoint& checkpoint,
    const std::string_view& key) {
  return find_checkpoint_tag_value(checkpoint.tags, key);
}

[[nodiscard]] inline std::vector<std::string> runtime_checkpoint_version_tags() {
  return {
      make_checkpoint_tag(kRuntimeCheckpointSchemaVersionTag, kRuntimeCheckpointSchemaVersion),
      make_checkpoint_tag(
          kRuntimeCheckpointFsmStateEnumVersionTag,
          kRuntimeCheckpointFsmStateEnumVersion),
      make_checkpoint_tag(
          kRuntimeCheckpointBudgetSchemaVersionTag,
          kRuntimeCheckpointBudgetSchemaVersion),
  };
}

inline void append_checkpoint_tag_if_missing(
    std::vector<std::string>& tags,
    const std::string_view& key,
    const std::string_view& value) {
  if (!find_checkpoint_tag_value(tags, key).has_value()) {
    tags.push_back(make_checkpoint_tag(key, value));
  }
}

[[nodiscard]] inline std::vector<std::string> with_runtime_checkpoint_version_tags(
    const std::vector<std::string>& tags) {
  auto merged = tags;
  append_checkpoint_tag_if_missing(
      merged,
      kRuntimeCheckpointSchemaVersionTag,
      kRuntimeCheckpointSchemaVersion);
  append_checkpoint_tag_if_missing(
      merged,
      kRuntimeCheckpointFsmStateEnumVersionTag,
      kRuntimeCheckpointFsmStateEnumVersion);
  append_checkpoint_tag_if_missing(
      merged,
      kRuntimeCheckpointBudgetSchemaVersionTag,
      kRuntimeCheckpointBudgetSchemaVersion);
  return merged;
}

}  // namespace dasall::runtime
#include "CheckpointManager.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CheckpointStateMapper.h"

namespace dasall::runtime {
namespace {

[[nodiscard]] bool is_waiting_checkpoint_state(const contracts::CheckpointState state) {
  return state == contracts::CheckpointState::Paused ||
         state == contracts::CheckpointState::WaitingConfirm ||
         state == contracts::CheckpointState::WaitingTool;
}

[[nodiscard]] CheckpointConsistencyReport reject_checkpoint(
    const CheckpointConsistencyIssue issue,
    const std::string& detail) {
  return make_checkpoint_rejected_report(issue, detail);
}

[[nodiscard]] std::optional<contracts::CheckpointState> resolve_checkpoint_state(
    const StateTransitionOutcome& outcome) {
  const auto mapped_state = CheckpointStateMapper::to_checkpoint_state(outcome.resolved_state);
  if (!mapped_state.has_value() || *mapped_state == contracts::CheckpointState::Unspecified) {
    return std::nullopt;
  }

  if (outcome.checkpoint_hint.checkpoint_state.has_value() &&
      outcome.checkpoint_hint.checkpoint_state != mapped_state) {
    return std::nullopt;
  }

  return mapped_state;
}

[[nodiscard]] CheckpointConsistencyReport validate_version_tag(
    const contracts::Checkpoint& checkpoint,
    const std::string_view key,
    const std::string_view expected_value,
    const CheckpointConsistencyIssue unsupported_issue) {
  const auto value = find_checkpoint_tag_value(checkpoint, key);
  if (!value.has_value()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingVersionTag,
        std::string("checkpoint is missing ") + std::string(key));
  }

  if (value.value() != expected_value) {
    return reject_checkpoint(
        unsupported_issue,
        std::string("checkpoint ") + std::string(key) +
            " is not compatible with runtime");
  }

  return make_checkpoint_consistent_report();
}

}  // namespace

CheckpointBuildResult CheckpointManager::build_checkpoint(
    const CheckpointBuildRequest& request) const {
  if (!request.transition_outcome.accepted) {
    return CheckpointBuildResult{
        .checkpoint = std::nullopt,
        .report = reject_checkpoint(
            CheckpointConsistencyIssue::TransitionRejected,
            "checkpoint build requires an accepted transition outcome"),
    };
  }

  const auto checkpoint_state = resolve_checkpoint_state(request.transition_outcome);
  if (!checkpoint_state.has_value()) {
    return CheckpointBuildResult{
        .checkpoint = std::nullopt,
        .report = reject_checkpoint(
            CheckpointConsistencyIssue::InvalidCheckpointState,
            "resolved runtime state cannot be folded into a compatible checkpoint state"),
    };
  }

  const contracts::Checkpoint checkpoint{
      .checkpoint_id = request.checkpoint_id,
      .state = checkpoint_state,
      .step_id = request.step_id,
      .working_memory_snapshot = request.working_memory_snapshot,
      .pending_action = request.pending_action.has_value()
                            ? request.pending_action
                            : std::optional<std::string>(std::string()),
      .request_id = request.request_id,
      .goal_id = request.goal_id,
      .belief_state_ref = request.belief_state_ref,
      .retry_count = request.retry_count,
      .created_at = request.created_at_ms,
      .tags = with_runtime_checkpoint_version_tags(request.tags),
  };

  const auto report = validate(checkpoint);
  if (report.consistent && checkpoint.checkpoint_id.has_value()) {
    const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    pending_runtime_budget_snapshots_[*checkpoint.checkpoint_id] =
        request.runtime_budget_snapshot;
  }

  return CheckpointBuildResult{
      .checkpoint = report.consistent ? std::optional<contracts::Checkpoint>(checkpoint)
                                      : std::nullopt,
      .report = report,
      .runtime_budget_snapshot = request.runtime_budget_snapshot,
  };
}

CheckpointPersistResult CheckpointManager::save(const contracts::Checkpoint& checkpoint) {
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  if (checkpoint.checkpoint_id.has_value()) {
    const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    const auto iterator = pending_runtime_budget_snapshots_.find(*checkpoint.checkpoint_id);
    if (iterator != pending_runtime_budget_snapshots_.end()) {
      runtime_budget_snapshot = iterator->second;
      pending_runtime_budget_snapshots_.erase(iterator);
    }
  }

  return save(checkpoint, runtime_budget_snapshot);
}

CheckpointPersistResult CheckpointManager::save(
    const contracts::Checkpoint& checkpoint,
    const std::optional<contracts::BudgetSnapshot>& runtime_budget_snapshot) {
  const auto report = validate(checkpoint);
  if (!report.consistent) {
    return CheckpointPersistResult{
        .persisted = false,
        .checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_411_CHECKPOINT_SAVE_FAILED,
        .detail = report.detail,
    };
  }

  const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    stored_checkpoints_[checkpoint.checkpoint_id.value()] = StoredCheckpointRecord{
      .checkpoint = checkpoint,
      .runtime_budget_snapshot = runtime_budget_snapshot,
    };
    pending_runtime_budget_snapshots_.erase(checkpoint.checkpoint_id.value());
  return CheckpointPersistResult{
      .persisted = true,
      .checkpoint_ref = checkpoint.checkpoint_id,
      .error_code = std::nullopt,
      .detail = "checkpoint persisted in memory store",
  };
}

CheckpointLoadResult CheckpointManager::load(const std::string& checkpoint_ref) const {
  std::optional<contracts::Checkpoint> checkpoint;
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  {
    const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    const auto iterator = stored_checkpoints_.find(checkpoint_ref);
    if (iterator == stored_checkpoints_.end()) {
      return CheckpointLoadResult{
          .checkpoint = std::nullopt,
          .report = reject_checkpoint(
              CheckpointConsistencyIssue::MissingCheckpointId,
              "checkpoint not found in memory store"),
          .error_code = RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT,
          .detail = "checkpoint not found in memory store",
      };
    }

    checkpoint = iterator->second.checkpoint;
    runtime_budget_snapshot = iterator->second.runtime_budget_snapshot;
  }

  const auto report = validate(*checkpoint);
  return CheckpointLoadResult{
      .checkpoint = report.consistent ? checkpoint : std::nullopt,
      .report = report,
      .error_code = report.consistent ? std::nullopt : report.error_code,
      .detail = report.consistent ? "checkpoint loaded from memory store" : report.detail,
      .runtime_budget_snapshot = report.consistent ? runtime_budget_snapshot : std::nullopt,
  };
}

CheckpointConsistencyReport CheckpointManager::validate(
    const contracts::Checkpoint& checkpoint) const {
  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingCheckpointId,
        "checkpoint_id must be populated");
  }

  if (!checkpoint.step_id.has_value() || checkpoint.step_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingStepId,
        "step_id must be populated");
  }

  if (!checkpoint.working_memory_snapshot.has_value() ||
      checkpoint.working_memory_snapshot->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingWorkingMemorySnapshot,
        "working_memory_snapshot must be populated");
  }

  if (!checkpoint.state.has_value() ||
      checkpoint.state == contracts::CheckpointState::Unspecified) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "checkpoint state must be concrete");
  }

  if (!checkpoint.pending_action.has_value()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingPendingAction,
        "pending_action must be present (use empty string for none)");
  }

  if (checkpoint.request_id.has_value() && checkpoint.request_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "request_id must be non-empty when present");
  }

  if (checkpoint.goal_id.has_value() && checkpoint.goal_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "goal_id must be non-empty when present");
  }

  if (checkpoint.belief_state_ref.has_value() && checkpoint.belief_state_ref->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "belief_state_ref must be non-empty when present");
  }

  if (checkpoint.created_at.has_value() && *checkpoint.created_at <= 0) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "created_at must be a positive timestamp when present");
  }

  if (checkpoint.tags.has_value()) {
    if (checkpoint.tags->empty()) {
      return reject_checkpoint(
          CheckpointConsistencyIssue::InvalidCheckpointState,
          "tags must contain at least one item when present");
    }

    for (const auto& tag : *checkpoint.tags) {
      if (tag.empty()) {
        return reject_checkpoint(
            CheckpointConsistencyIssue::InvalidCheckpointState,
            "tags must not contain empty strings");
      }
    }
  }

  if (is_waiting_checkpoint_state(*checkpoint.state) && checkpoint.pending_action->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingPendingAction,
        "waiting checkpoints must persist non-empty pending_action");
  }

  auto report = validate_version_tag(
      checkpoint,
      kRuntimeCheckpointSchemaVersionTag,
      kRuntimeCheckpointSchemaVersion,
      CheckpointConsistencyIssue::UnsupportedSchemaVersion);
  if (report.rejected()) {
    return report;
  }

  report = validate_version_tag(
      checkpoint,
      kRuntimeCheckpointFsmStateEnumVersionTag,
      kRuntimeCheckpointFsmStateEnumVersion,
      CheckpointConsistencyIssue::UnsupportedFsmStateVersion);
  if (report.rejected()) {
    return report;
  }

  report = validate_version_tag(
      checkpoint,
      kRuntimeCheckpointBudgetSchemaVersionTag,
      kRuntimeCheckpointBudgetSchemaVersion,
      CheckpointConsistencyIssue::UnsupportedBudgetSchemaVersion);
  if (report.rejected()) {
    return report;
  }

  return make_checkpoint_consistent_report("checkpoint is structurally resumable");
}

ResumePlanDecision CheckpointManager::make_resume_plan(
    const contracts::Checkpoint& checkpoint) const {
  const auto report = validate(checkpoint);
  if (!report.consistent) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        report.detail,
        report.error_code);
  }

  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    return make_rejected_resume_plan(
        ResumePlanViolation::MissingCheckpointReference,
        "checkpoint_id is required to build a resume plan");
  }

  const auto target_state = resume_target_state(*checkpoint.state);
  if (!target_state.has_value()) {
    return make_rejected_resume_plan(
        ResumePlanViolation::UnsupportedCheckpointState,
        "terminal or unspecified checkpoints cannot be resumed");
  }

  if (is_waiting_checkpoint_state(*checkpoint.state) &&
      (!checkpoint.pending_action.has_value() || checkpoint.pending_action->empty())) {
    return make_rejected_resume_plan(
        ResumePlanViolation::MissingPendingAction,
        "resume plan requires pending_action for waiting checkpoints");
  }

  return make_resume_plan_decision(
      ResumePlan{
          .checkpoint_ref = *checkpoint.checkpoint_id,
          .target_state = *target_state,
          .checkpoint_state = *checkpoint.state,
          .resume_token = std::string(),
          .resume_reason = "resume plan synthesized from checkpoint",
          .pending_action = checkpoint.pending_action,
          .policy_snapshot_ref = std::nullopt,
          .requires_operator_intervention =
              checkpoint.state == contracts::CheckpointState::WaitingConfirm,
      },
      "checkpoint is resumable");
}

ResumePlanDecision CheckpointManager::make_resume_plan(
    const contracts::Checkpoint& checkpoint,
    const ResumeSeed& resume_seed) const {
  if (!resume_seed.has_minimum_requirements()) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        "resume seed must include checkpoint_ref, resume_token and resume_reason");
  }

  const auto base_decision = make_resume_plan(checkpoint);
  if (base_decision.rejected() || !base_decision.plan.has_value()) {
    return base_decision;
  }

  if (base_decision.plan->checkpoint_ref != resume_seed.checkpoint_ref) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        "resume seed checkpoint_ref does not match checkpoint anchor");
  }

  if (checkpoint.request_id.has_value() && checkpoint.request_id != resume_seed.request_id) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        "resume seed request_id does not match checkpoint request_id");
  }

  auto plan = *base_decision.plan;
  plan.resume_token = resume_seed.resume_token;
  plan.resume_reason = resume_seed.resume_reason;
  plan.policy_snapshot_ref = resume_seed.policy_snapshot_ref;
  return make_resume_plan_decision(plan, "checkpoint is resumable with session resume seed");
}

void CheckpointManager::seed_for_test(
    const contracts::Checkpoint& checkpoint,
    std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot) {
  const std::lock_guard<std::mutex> lock(ckpt_mutex_);
  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    return;
  }

  stored_checkpoints_[*checkpoint.checkpoint_id] = StoredCheckpointRecord{
      .checkpoint = checkpoint,
      .runtime_budget_snapshot = std::move(runtime_budget_snapshot),
  };
}

}  // namespace dasall::runtime
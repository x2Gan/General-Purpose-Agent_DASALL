#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/ICheckpointManager.h"
#include "support/TestAssertions.h"

namespace {

class FakeCheckpointManager final : public dasall::runtime::ICheckpointManager {
 public:
  [[nodiscard]] dasall::runtime::CheckpointBuildResult build_checkpoint(
      const dasall::runtime::CheckpointBuildRequest& request) const override {
    using dasall::runtime::CheckpointConsistencyIssue;
    using dasall::runtime::make_checkpoint_rejected_report;

    if (!request.transition_outcome.accepted) {
      return dasall::runtime::CheckpointBuildResult{
          .checkpoint = std::nullopt,
          .report = make_checkpoint_rejected_report(
              CheckpointConsistencyIssue::TransitionRejected,
              "checkpoint build requires an accepted transition outcome"),
      };
    }

    if (!request.transition_outcome.checkpoint_hint.checkpoint_state.has_value()) {
      return dasall::runtime::CheckpointBuildResult{
          .checkpoint = std::nullopt,
          .report = make_checkpoint_rejected_report(
              CheckpointConsistencyIssue::InvalidCheckpointState,
              "checkpoint hint must carry a concrete checkpoint state"),
      };
    }

    const auto pending_action = request.pending_action.has_value()
                                    ? request.pending_action
                                    : std::optional<std::string>(std::string());
    const dasall::contracts::Checkpoint checkpoint{
        .checkpoint_id = request.checkpoint_id,
        .state = request.transition_outcome.checkpoint_hint.checkpoint_state,
        .step_id = request.step_id,
        .working_memory_snapshot = request.working_memory_snapshot,
        .pending_action = pending_action,
        .request_id = request.request_id,
        .goal_id = request.goal_id,
        .belief_state_ref = request.belief_state_ref,
        .retry_count = request.retry_count,
        .created_at = request.created_at_ms,
        .tags = dasall::runtime::with_runtime_checkpoint_version_tags(request.tags),
    };

    const auto report = validate(checkpoint);
    return dasall::runtime::CheckpointBuildResult{
        .checkpoint = report.consistent ? std::optional<dasall::contracts::Checkpoint>(checkpoint)
                                        : std::nullopt,
        .report = report,
    };
  }

  [[nodiscard]] dasall::runtime::CheckpointPersistResult save(
      const dasall::contracts::Checkpoint& checkpoint) override {
    const auto report = validate(checkpoint);
    if (!report.consistent) {
      return dasall::runtime::CheckpointPersistResult{
          .persisted = false,
          .checkpoint_ref = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_411_CHECKPOINT_SAVE_FAILED,
          .detail = report.detail,
      };
    }

    stored_checkpoint_ = checkpoint;
    return dasall::runtime::CheckpointPersistResult{
        .persisted = true,
        .checkpoint_ref = checkpoint.checkpoint_id,
        .error_code = std::nullopt,
        .detail = "checkpoint persisted in fake store",
    };
  }

  [[nodiscard]] dasall::runtime::CheckpointLoadResult load(
      const std::string& checkpoint_ref) const override {
    using dasall::runtime::CheckpointConsistencyIssue;
    using dasall::runtime::make_checkpoint_rejected_report;

    if (!stored_checkpoint_.has_value() || !stored_checkpoint_->checkpoint_id.has_value() ||
        stored_checkpoint_->checkpoint_id.value() != checkpoint_ref) {
      return dasall::runtime::CheckpointLoadResult{
          .checkpoint = std::nullopt,
          .report = make_checkpoint_rejected_report(
              CheckpointConsistencyIssue::MissingCheckpointId,
              "checkpoint not found in fake store"),
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT,
          .detail = "checkpoint not found in fake store",
      };
    }

    const auto report = validate(*stored_checkpoint_);
    return dasall::runtime::CheckpointLoadResult{
        .checkpoint = report.consistent ? stored_checkpoint_ : std::nullopt,
        .report = report,
        .error_code = report.consistent ? std::nullopt : report.error_code,
        .detail = report.consistent ? "checkpoint loaded from fake store" : report.detail,
    };
  }

  [[nodiscard]] dasall::runtime::CheckpointConsistencyReport validate(
      const dasall::contracts::Checkpoint& checkpoint) const override {
    using dasall::contracts::CheckpointState;
    using dasall::runtime::CheckpointConsistencyIssue;
    using dasall::runtime::kRuntimeCheckpointBudgetSchemaVersion;
    using dasall::runtime::kRuntimeCheckpointBudgetSchemaVersionTag;
    using dasall::runtime::kRuntimeCheckpointFsmStateEnumVersion;
    using dasall::runtime::kRuntimeCheckpointFsmStateEnumVersionTag;
    using dasall::runtime::kRuntimeCheckpointSchemaVersion;
    using dasall::runtime::kRuntimeCheckpointSchemaVersionTag;
    using dasall::runtime::make_checkpoint_consistent_report;
    using dasall::runtime::make_checkpoint_rejected_report;

    if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingCheckpointId,
          "checkpoint_id must be populated");
    }

    if (!checkpoint.step_id.has_value() || checkpoint.step_id->empty()) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingStepId,
          "step_id must be populated");
    }

    if (!checkpoint.working_memory_snapshot.has_value() ||
        checkpoint.working_memory_snapshot->empty()) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingWorkingMemorySnapshot,
          "working_memory_snapshot must be populated");
    }

    if (!checkpoint.state.has_value() || checkpoint.state == CheckpointState::Unspecified) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::InvalidCheckpointState,
          "checkpoint state must be concrete");
    }

    if ((checkpoint.state == CheckpointState::Paused ||
         checkpoint.state == CheckpointState::WaitingConfirm ||
         checkpoint.state == CheckpointState::WaitingTool) &&
        (!checkpoint.pending_action.has_value() || checkpoint.pending_action->empty())) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingPendingAction,
          "waiting checkpoints must persist pending_action");
    }

    const auto schema_version =
        dasall::runtime::find_checkpoint_tag_value(checkpoint, kRuntimeCheckpointSchemaVersionTag);
    if (!schema_version.has_value()) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingVersionTag,
          "checkpoint is missing rt.schema_version");
    }

    if (schema_version.value() != kRuntimeCheckpointSchemaVersion) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::UnsupportedSchemaVersion,
          "checkpoint schema version is not compatible with runtime");
    }

    const auto fsm_version = dasall::runtime::find_checkpoint_tag_value(
        checkpoint,
        kRuntimeCheckpointFsmStateEnumVersionTag);
    if (!fsm_version.has_value()) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingVersionTag,
          "checkpoint is missing rt.fsm_state_enum_version");
    }

    if (fsm_version.value() != kRuntimeCheckpointFsmStateEnumVersion) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::UnsupportedFsmStateVersion,
          "checkpoint fsm state version is not compatible with runtime");
    }

    const auto budget_version = dasall::runtime::find_checkpoint_tag_value(
        checkpoint,
        kRuntimeCheckpointBudgetSchemaVersionTag);
    if (!budget_version.has_value()) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::MissingVersionTag,
          "checkpoint is missing rt.budget_schema_version");
    }

    if (budget_version.value() != kRuntimeCheckpointBudgetSchemaVersion) {
      return make_checkpoint_rejected_report(
          CheckpointConsistencyIssue::UnsupportedBudgetSchemaVersion,
          "checkpoint budget schema version is not compatible with runtime");
    }

    return make_checkpoint_consistent_report("checkpoint is structurally resumable");
  }

  [[nodiscard]] dasall::runtime::ResumePlanDecision make_resume_plan(
      const dasall::contracts::Checkpoint& checkpoint) const override {
    using dasall::contracts::CheckpointState;
    using dasall::runtime::ResumePlan;
    using dasall::runtime::ResumePlanViolation;
    using dasall::runtime::make_rejected_resume_plan;
    using dasall::runtime::make_resume_plan_decision;
    using dasall::runtime::resume_target_state;

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

    if ((checkpoint.state == CheckpointState::Paused ||
         checkpoint.state == CheckpointState::WaitingConfirm ||
         checkpoint.state == CheckpointState::WaitingTool) &&
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
            .resume_reason = "resume plan synthesized from checkpoint",
            .pending_action = checkpoint.pending_action,
            .policy_snapshot_ref = std::nullopt,
            .requires_operator_intervention =
                checkpoint.state == CheckpointState::WaitingConfirm,
        },
        "checkpoint is resumable");
  }

  void seed_for_test(const dasall::contracts::Checkpoint& checkpoint) {
    stored_checkpoint_ = checkpoint;
  }

 private:
  std::optional<dasall::contracts::Checkpoint> stored_checkpoint_;
};

[[nodiscard]] dasall::runtime::StateTransitionOutcome make_waiting_clarify_outcome() {
  return dasall::runtime::make_transition_outcome(
      dasall::runtime::StateTransitionRequest{
          .from_state = dasall::runtime::RuntimeState::Reasoning,
          .requested_to = dasall::runtime::RuntimeState::WaitingClarify,
          .transition_reason = "clarification required",
          .guard_facts = {
              dasall::runtime::TransitionGuardFact::ClarificationNeeded,
              dasall::runtime::TransitionGuardFact::ProfileAllowsClarify,
          },
      },
      dasall::runtime::RuntimeState::WaitingClarify,
      dasall::runtime::StateTransitionCheckpointHint{
          .mutation = dasall::runtime::CheckpointMutationKind::Write,
          .checkpoint_state = dasall::contracts::CheckpointState::Paused,
          .pending_action_required = true,
      });
}

}  // namespace

int main() {
  using dasall::contracts::CheckpointState;
  using dasall::runtime::CheckpointConsistencyIssue;
  using dasall::runtime::ResumePlanViolation;
  using dasall::runtime::RuntimeErrorCode;
  using dasall::runtime::RuntimeState;
  using dasall::runtime::find_checkpoint_tag_value;
  using dasall::runtime::kRuntimeCheckpointBudgetSchemaVersion;
  using dasall::runtime::kRuntimeCheckpointBudgetSchemaVersionTag;
  using dasall::runtime::kRuntimeCheckpointFsmStateEnumVersion;
  using dasall::runtime::kRuntimeCheckpointFsmStateEnumVersionTag;
  using dasall::runtime::kRuntimeCheckpointSchemaVersion;
  using dasall::runtime::kRuntimeCheckpointSchemaVersionTag;
  using dasall::runtime::make_checkpoint_tag;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    FakeCheckpointManager manager;

    const auto build_result = manager.build_checkpoint(
        dasall::runtime::CheckpointBuildRequest{
            .transition_outcome = make_waiting_clarify_outcome(),
            .checkpoint_id = "chk-001",
            .step_id = "clarify-user",
            .working_memory_snapshot = "wm:clarify:1",
            .pending_action = std::string("wait for user clarification"),
            .request_id = std::string("req-001"),
            .goal_id = std::string("goal-001"),
            .belief_state_ref = std::string("belief-001"),
            .retry_count = 1,
            .created_at_ms = 1700000001,
            .runtime_budget_snapshot = std::nullopt,
            .tags = {"audit=unit"},
        });

    assert_true(build_result.built(), "accepted waiting transition should build a checkpoint");
    assert_true(build_result.checkpoint.has_value(), "build result should carry checkpoint value");
    assert_true(build_result.report.consistent, "built checkpoint should pass consistency validation");
    assert_equal(
        std::string(kRuntimeCheckpointSchemaVersion),
        find_checkpoint_tag_value(build_result.checkpoint->tags, kRuntimeCheckpointSchemaVersionTag)
            .value_or(std::string()),
        "checkpoint should carry schema version tag");
    assert_equal(
        std::string(kRuntimeCheckpointFsmStateEnumVersion),
        find_checkpoint_tag_value(
            build_result.checkpoint->tags,
            kRuntimeCheckpointFsmStateEnumVersionTag)
            .value_or(std::string()),
        "checkpoint should carry fsm version tag");
    assert_equal(
        std::string(kRuntimeCheckpointBudgetSchemaVersion),
        find_checkpoint_tag_value(
            build_result.checkpoint->tags,
            kRuntimeCheckpointBudgetSchemaVersionTag)
            .value_or(std::string()),
        "checkpoint should carry budget version tag");

    const auto persist_result = manager.save(*build_result.checkpoint);
    assert_true(persist_result.persisted, "consistent checkpoint should persist through fake store");
    assert_equal("chk-001", persist_result.checkpoint_ref.value_or(std::string()),
                 "persist should expose checkpoint_ref");

    const auto load_result = manager.load("chk-001");
    assert_true(load_result.loaded(), "persisted checkpoint should load successfully");
    assert_true(load_result.checkpoint.has_value(), "load should return checkpoint payload");
    assert_true(load_result.report.issue == CheckpointConsistencyIssue::None,
                "loaded checkpoint should remain consistent");

    const auto resume_plan = manager.make_resume_plan(*load_result.checkpoint);
    assert_true(resume_plan.resumable, "paused waiting checkpoint should yield a resume plan");
    assert_true(resume_plan.plan.has_value(), "resume decision should carry concrete plan");
    assert_true(resume_plan.plan->target_state == RuntimeState::WaitingClarify,
                "paused checkpoints should resume into WaitingClarify");
    assert_true(resume_plan.plan->has_pending_action(),
                "waiting resume plan should preserve pending action");
    assert_true(!resume_plan.plan->requires_operator_intervention,
                "clarification checkpoint should not require operator intervention");

    dasall::contracts::Checkpoint incompatible_checkpoint = *build_result.checkpoint;
    incompatible_checkpoint.checkpoint_id = std::string("chk-unsupported");
    incompatible_checkpoint.tags = std::vector<std::string>{
        make_checkpoint_tag(kRuntimeCheckpointSchemaVersionTag, "2"),
        make_checkpoint_tag(
            kRuntimeCheckpointFsmStateEnumVersionTag,
            kRuntimeCheckpointFsmStateEnumVersion),
        make_checkpoint_tag(
            kRuntimeCheckpointBudgetSchemaVersionTag,
            kRuntimeCheckpointBudgetSchemaVersion),
    };
    manager.seed_for_test(incompatible_checkpoint);

    const auto incompatible_load = manager.load("chk-unsupported");
    assert_true(!incompatible_load.loaded(),
                "checkpoint with unsupported schema version must be rejected on load");
    assert_true(incompatible_load.error_code == RuntimeErrorCode::RT_E_412_RESUME_REJECTED,
                "schema-version mismatch should map to RT_E_412_RESUME_REJECTED");
    assert_true(incompatible_load.report.issue == CheckpointConsistencyIssue::UnsupportedSchemaVersion,
                "load rejection should classify unsupported schema version");

    dasall::contracts::Checkpoint terminal_checkpoint = *build_result.checkpoint;
    terminal_checkpoint.state = CheckpointState::Succeeded;
    const auto terminal_resume = manager.make_resume_plan(terminal_checkpoint);
    assert_true(!terminal_resume.resumable,
                "terminal checkpoint must not generate a resume plan");
    assert_true(terminal_resume.violation == ResumePlanViolation::UnsupportedCheckpointState,
                "terminal resume rejection should classify unsupported checkpoint state");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
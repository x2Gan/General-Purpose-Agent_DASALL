#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/CheckpointManager.h"
#include "support/TestAssertions.h"

namespace {

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
    dasall::runtime::CheckpointManager manager;

    const auto derived_state_result = manager.build_checkpoint(
      dasall::runtime::CheckpointBuildRequest{
        .transition_outcome = dasall::runtime::make_transition_outcome(
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
            .checkpoint_state = std::nullopt,
            .pending_action_required = true,
          }),
        .checkpoint_id = "chk-derived",
        .step_id = "clarify-derived",
        .working_memory_snapshot = "wm:clarify:derived",
        .pending_action = std::string("wait for user clarification"),
        .request_id = std::nullopt,
        .goal_id = std::nullopt,
        .belief_state_ref = std::nullopt,
        .retry_count = std::nullopt,
        .created_at_ms = std::nullopt,
        .runtime_budget_snapshot = std::nullopt,
        .tags = {"audit=unit"},
      });

    assert_true(derived_state_result.built(),
          "checkpoint state should be derived from CheckpointStateMapper when hint omits it");
    assert_true(derived_state_result.checkpoint->state == CheckpointState::Paused,
          "waiting clarify state should fold to paused checkpoint state");

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
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "session/SessionManager.h"
#include "support/TestAssertions.h"

int main() {
  using dasall::runtime::PendingInteractionKind;
  using dasall::runtime::RuntimeErrorCode;
  using dasall::runtime::RuntimeState;
  using dasall::runtime::pending_interaction_kind_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    assert_equal("Clarify", std::string(pending_interaction_kind_name(PendingInteractionKind::Clarify)),
                 "pending interaction enum should expose stable Clarify name");
    assert_equal("WaitExternal",
                 std::string(pending_interaction_kind_name(PendingInteractionKind::WaitExternal)),
                 "pending interaction enum should expose stable WaitExternal name");

    dasall::runtime::SessionManager manager;
    const auto load_result = manager.load_session(
        dasall::runtime::SessionLoadRequest{
            .session_id = "session-018",
            .request_id = "req-018",
            .checkpoint_ref = std::nullopt,
            .allow_session_create = true,
        });

    assert_true(load_result.has_snapshot(), "load_session should create a new snapshot when allowed");
    assert_true(load_result.created_new_session,
                "first load should surface created_new_session=true in session manager");
    assert_true(!load_result.snapshot->has_active_checkpoint(),
                "newly created snapshot should not have an active checkpoint ref");

    const auto bind_result = manager.bind_checkpoint_ref(
        dasall::runtime::BindCheckpointRefRequest{
            .session_id = "session-018",
            .request_id = "req-018",
            .checkpoint_ref = "chk-018",
            .fsm_state = RuntimeState::WaitingClarify,
            .pending_interaction = dasall::runtime::PendingInteractionState{
                .interaction_kind = PendingInteractionKind::Clarify,
                .prompt_token = "prompt-clarify-018",
                .deadline_ms = 1700001818,
                .blocking_reason = "await clarification",
                .resume_channel = "user_reply",
                .input_schema_hint = "text/plain",
            },
        });

    assert_true(bind_result.persisted, "bind_checkpoint_ref should persist active checkpoint anchor");
    assert_equal("chk-018", bind_result.active_checkpoint_ref.value_or(std::string()),
                 "bind_checkpoint_ref should expose active checkpoint ref");

    const auto mismatched_load = manager.load_session(
        dasall::runtime::SessionLoadRequest{
            .session_id = "session-018",
            .request_id = "req-018",
            .checkpoint_ref = std::string("chk-other"),
            .allow_session_create = false,
        });
    assert_true(!mismatched_load.accepted,
                "load_session should reject mismatched checkpoint anchor requests");
    assert_true(mismatched_load.error_code == RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
                "mismatched checkpoint anchor should map to RT_E_401_SESSION_INCONSISTENT");

    const auto waiting_snapshot = dasall::runtime::SessionSnapshot{
        .session_id = "session-018",
        .request_id = "req-018",
        .turn_index = 3,
        .active_checkpoint_ref = std::string("chk-018"),
        .fsm_state = RuntimeState::WaitingClarify,
        .budget_snapshot_ref = std::string("budget-018"),
        .pending_interaction = dasall::runtime::PendingInteractionState{
            .interaction_kind = PendingInteractionKind::Clarify,
            .prompt_token = "prompt-clarify-018",
            .deadline_ms = 1700001818,
            .blocking_reason = "await clarification",
            .resume_channel = "user_reply",
            .input_schema_hint = "text/plain",
        },
        .last_result_summary = std::string("clarification requested"),
    };

    const auto prepare_result = manager.prepare_turn(
        dasall::runtime::PrepareTurnRequest{
            .session_snapshot = waiting_snapshot,
            .resume_turn = true,
            .expected_checkpoint_ref = std::string("chk-018"),
        });
    assert_true(prepare_result.accepted,
                "waiting snapshot with matching checkpoint should prepare successfully");
    assert_true(prepare_result.effective_session.has_value(),
                "prepare_turn should expose effective session snapshot");

    const auto resume_seed_result = manager.build_resume_seed(
        dasall::runtime::BuildResumeSeedRequest{
            .session_snapshot = waiting_snapshot,
            .checkpoint_ref = "chk-018",
            .resume_reason = "resume after user clarification",
            .policy_snapshot_ref = std::string("policy-018"),
        });
    assert_true(resume_seed_result.built(),
                "matching session checkpoint should build a resume seed");
    assert_true(resume_seed_result.resume_seed->has_minimum_requirements(),
                "resume seed should satisfy minimum requirements");
    assert_true(resume_seed_result.resume_seed->pending_interaction.has_value(),
                "resume seed should carry pending interaction state");

    const auto persist_result = manager.persist_turn(
        dasall::runtime::SessionPersistRequest{
            .session_snapshot = waiting_snapshot,
            .terminal_state = RuntimeState::Completed,
            .checkpoint_ref = std::string("chk-018"),
            .audit_summary = std::string("clarification consumed and turn completed"),
            .next_resume_seed_ref = std::nullopt,
        });
    assert_true(persist_result.persisted, "persist_turn should succeed for non-idle terminal state");
    assert_equal("chk-018", persist_result.active_checkpoint_ref.value_or(std::string()),
                 "persist_turn should keep active checkpoint ref when provided");

    const auto mismatched_resume_seed = manager.build_resume_seed(
        dasall::runtime::BuildResumeSeedRequest{
            .session_snapshot = waiting_snapshot,
            .checkpoint_ref = "chk-other",
            .resume_reason = "resume with wrong anchor",
            .policy_snapshot_ref = std::nullopt,
        });
    assert_true(!mismatched_resume_seed.built(),
                "resume seed should reject mismatched active checkpoint anchor");
    assert_true(mismatched_resume_seed.error_code == RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
                "mismatched checkpoint anchor should map to RT_E_401_SESSION_INCONSISTENT");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
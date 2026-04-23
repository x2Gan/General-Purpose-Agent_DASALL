#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "AgentTypes.h"
#include "session/ISessionManager.h"
#include "support/TestAssertions.h"

namespace {

class FakeSessionManager final : public dasall::runtime::ISessionManager {
 public:
  [[nodiscard]] dasall::runtime::SessionLoadResult load_session(
      const dasall::runtime::SessionLoadRequest& request) const override {
    if (!request.has_minimum_requirements()) {
      return dasall::runtime::SessionLoadResult{
          .accepted = false,
          .created_new_session = false,
          .snapshot = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_400_SESSION_NOT_FOUND,
          .detail = "session_id and request_id are required",
      };
    }

    if (stored_snapshot_.has_value()) {
      if (request.checkpoint_ref.has_value() &&
          stored_snapshot_->active_checkpoint_ref != request.checkpoint_ref) {
        return dasall::runtime::SessionLoadResult{
            .accepted = false,
            .created_new_session = false,
            .snapshot = std::nullopt,
            .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
            .detail = "requested checkpoint_ref does not match active session anchor",
        };
      }

      return dasall::runtime::make_session_load_result(
          *stored_snapshot_,
          false,
          "existing session loaded from fake store");
    }

    if (!request.allow_session_create) {
      return dasall::runtime::SessionLoadResult{
          .accepted = false,
          .created_new_session = false,
          .snapshot = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_400_SESSION_NOT_FOUND,
          .detail = "session does not exist and creation is disabled",
      };
    }

    return dasall::runtime::make_session_load_result(
        dasall::runtime::SessionSnapshot{
            .session_id = request.session_id,
            .request_id = request.request_id,
            .turn_index = 0,
            .active_checkpoint_ref = request.checkpoint_ref,
            .fsm_state = dasall::runtime::RuntimeState::Idle,
            .budget_snapshot_ref = std::nullopt,
            .pending_interaction = std::nullopt,
            .last_result_summary = std::nullopt,
        },
        true,
        "new session snapshot created for surface test");
  }

  [[nodiscard]] dasall::runtime::PrepareTurnResult prepare_turn(
      const dasall::runtime::PrepareTurnRequest& request) const override {
    const auto& snapshot = request.session_snapshot;
    if (request.expected_checkpoint_ref.has_value() &&
        snapshot.active_checkpoint_ref != request.expected_checkpoint_ref) {
      return dasall::runtime::PrepareTurnResult{
          .accepted = false,
          .effective_session = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "prepare_turn expected checkpoint_ref does not match session snapshot",
      };
    }

    if (snapshot.pending_interaction.has_value() && snapshot.pending_interaction->active() &&
        snapshot.fsm_state != dasall::runtime::RuntimeState::WaitingClarify &&
        snapshot.fsm_state != dasall::runtime::RuntimeState::WaitingConfirm &&
        snapshot.fsm_state != dasall::runtime::RuntimeState::WaitingExternal) {
      return dasall::runtime::PrepareTurnResult{
          .accepted = false,
          .effective_session = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "pending interaction requires a waiting-state session snapshot",
      };
    }

    return dasall::runtime::make_prepare_turn_result(
        snapshot,
        request.resume_turn ? "resume turn prepared" : "fresh turn prepared");
  }

  [[nodiscard]] dasall::runtime::SessionPersistResult persist_turn(
      const dasall::runtime::SessionPersistRequest& request) override {
    if (!request.has_minimum_requirements() || request.terminal_state == dasall::runtime::RuntimeState::Idle) {
      return dasall::runtime::SessionPersistResult{
          .persisted = false,
          .active_checkpoint_ref = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "persist_turn requires session identity and a non-idle terminal state",
      };
    }

    stored_snapshot_ = request.session_snapshot;
    stored_snapshot_->fsm_state = request.terminal_state;
    if (request.checkpoint_ref.has_value()) {
      stored_snapshot_->active_checkpoint_ref = request.checkpoint_ref;
    }
    return dasall::runtime::make_session_persist_result(
        stored_snapshot_->active_checkpoint_ref,
        "turn persisted in fake session store");
  }

  [[nodiscard]] dasall::runtime::SessionPersistResult bind_checkpoint_ref(
      const dasall::runtime::BindCheckpointRefRequest& request) override {
    if (!request.has_minimum_requirements()) {
      return dasall::runtime::SessionPersistResult{
          .persisted = false,
          .active_checkpoint_ref = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "bind_checkpoint_ref requires session_id, request_id and checkpoint_ref",
      };
    }

    if (!stored_snapshot_.has_value()) {
      stored_snapshot_ = dasall::runtime::SessionSnapshot{
          .session_id = request.session_id,
          .request_id = request.request_id,
          .turn_index = 0,
          .active_checkpoint_ref = request.checkpoint_ref,
          .fsm_state = request.fsm_state,
          .budget_snapshot_ref = std::nullopt,
          .pending_interaction = request.pending_interaction,
          .last_result_summary = std::nullopt,
      };
    } else {
      stored_snapshot_->session_id = request.session_id;
      stored_snapshot_->request_id = request.request_id;
      stored_snapshot_->active_checkpoint_ref = request.checkpoint_ref;
      stored_snapshot_->fsm_state = request.fsm_state;
      stored_snapshot_->pending_interaction = request.pending_interaction;
    }

    return dasall::runtime::make_session_persist_result(
        stored_snapshot_->active_checkpoint_ref,
        "checkpoint reference bound to fake session");
  }

  [[nodiscard]] dasall::runtime::ResumeSeedResult build_resume_seed(
      const dasall::runtime::BuildResumeSeedRequest& request) const override {
    if (!request.has_minimum_requirements()) {
      return dasall::runtime::ResumeSeedResult{
          .resume_seed = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "build_resume_seed requires session identity, checkpoint_ref and resume_reason",
      };
    }

    if (request.session_snapshot.active_checkpoint_ref.has_value() &&
        request.session_snapshot.active_checkpoint_ref != request.checkpoint_ref) {
      return dasall::runtime::ResumeSeedResult{
          .resume_seed = std::nullopt,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "resume seed checkpoint_ref must match active session anchor",
      };
    }

    return dasall::runtime::make_resume_seed_result(
        dasall::runtime::ResumeSeed{
            .session_id = request.session_snapshot.session_id,
            .request_id = request.session_snapshot.request_id,
            .checkpoint_ref = request.checkpoint_ref,
        .resume_token = request.resume_token,
            .fsm_state = request.session_snapshot.fsm_state,
            .pending_interaction = request.session_snapshot.pending_interaction,
            .policy_snapshot_ref = request.policy_snapshot_ref,
            .resume_reason = request.resume_reason,
        },
        "resume seed built from fake session snapshot");
  }

 private:
  mutable std::optional<dasall::runtime::SessionSnapshot> stored_snapshot_;
};

}  // namespace

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

    FakeSessionManager manager;
    const auto load_result = manager.load_session(
        dasall::runtime::SessionLoadRequest{
            .session_id = "session-010",
            .request_id = "req-010",
            .checkpoint_ref = std::nullopt,
            .allow_session_create = true,
        });

    assert_true(load_result.has_snapshot(), "load_session should create a new snapshot when allowed");
    assert_true(load_result.created_new_session,
                "first load should surface created_new_session=true in fake manager");
    assert_true(!load_result.snapshot->has_active_checkpoint(),
                "newly created snapshot should not have an active checkpoint ref");

    const auto bind_result = manager.bind_checkpoint_ref(
        dasall::runtime::BindCheckpointRefRequest{
            .session_id = "session-010",
            .request_id = "req-010",
            .checkpoint_ref = "chk-010",
            .fsm_state = RuntimeState::WaitingClarify,
            .pending_interaction = dasall::runtime::PendingInteractionState{
                .interaction_kind = PendingInteractionKind::Clarify,
                .prompt_token = "prompt-clarify-010",
                .deadline_ms = 1700001010,
                .blocking_reason = "await clarification",
                .resume_channel = "user_reply",
                .input_schema_hint = "text/plain",
            },
        });

    assert_true(bind_result.persisted, "bind_checkpoint_ref should persist active checkpoint anchor");
    assert_equal("chk-010", bind_result.active_checkpoint_ref.value_or(std::string()),
                 "bind_checkpoint_ref should expose active checkpoint ref");

    const auto waiting_snapshot = dasall::runtime::SessionSnapshot{
        .session_id = "session-010",
        .request_id = "req-010",
        .turn_index = 3,
        .active_checkpoint_ref = std::string("chk-010"),
        .fsm_state = RuntimeState::WaitingClarify,
        .budget_snapshot_ref = std::string("budget-010"),
        .pending_interaction = dasall::runtime::PendingInteractionState{
            .interaction_kind = PendingInteractionKind::Clarify,
            .prompt_token = "prompt-clarify-010",
            .deadline_ms = 1700001010,
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
            .expected_checkpoint_ref = std::string("chk-010"),
        });
    assert_true(prepare_result.accepted,
                "waiting snapshot with matching checkpoint should prepare successfully");
    assert_true(prepare_result.effective_session.has_value(),
                "prepare_turn should expose effective session snapshot");

    const auto resume_seed_result = manager.build_resume_seed(
        dasall::runtime::BuildResumeSeedRequest{
            .session_snapshot = waiting_snapshot,
            .checkpoint_ref = "chk-010",
        .resume_token = dasall::runtime::make_resume_binding_token("session-010", "chk-010"),
            .resume_reason = "resume after user clarification",
            .policy_snapshot_ref = std::string("policy-010"),
        });
    assert_true(resume_seed_result.built(),
                "matching session checkpoint should build a resume seed");
    assert_true(resume_seed_result.resume_seed->has_minimum_requirements(),
                "resume seed should satisfy minimum requirements");
    assert_true(resume_seed_result.resume_seed->pending_interaction.has_value(),
                "resume seed should carry pending interaction state");
    assert_equal(
      dasall::runtime::make_resume_binding_token("session-010", "chk-010"),
      resume_seed_result.resume_seed->resume_token,
      "resume seed should preserve the resume binding token");

    const auto persist_result = manager.persist_turn(
        dasall::runtime::SessionPersistRequest{
            .session_snapshot = waiting_snapshot,
            .terminal_state = RuntimeState::Completed,
            .checkpoint_ref = std::string("chk-010"),
            .audit_summary = std::string("clarification consumed and turn completed"),
            .next_resume_seed_ref = std::nullopt,
        });
    assert_true(persist_result.persisted, "persist_turn should succeed for non-idle terminal state");
    assert_equal("chk-010", persist_result.active_checkpoint_ref.value_or(std::string()),
                 "persist_turn should keep active checkpoint ref when provided");

    const auto mismatched_resume_seed = manager.build_resume_seed(
        dasall::runtime::BuildResumeSeedRequest{
            .session_snapshot = waiting_snapshot,
            .checkpoint_ref = "chk-other",
        .resume_token = dasall::runtime::make_resume_binding_token("session-010", "chk-other"),
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
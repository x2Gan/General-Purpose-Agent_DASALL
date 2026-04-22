#include "SessionManager.h"

#include <optional>
#include <string>

namespace dasall::runtime {

void SessionManager::seed_for_test(const SessionSnapshot& session_snapshot) {
  const std::lock_guard<std::mutex> lock(session_mutex_);
  stored_snapshot_ = session_snapshot;
}

SessionLoadResult SessionManager::load_session(
    const SessionLoadRequest& request) const {
  if (!request.has_minimum_requirements()) {
    return SessionLoadResult{
        .accepted = false,
        .created_new_session = false,
        .snapshot = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_400_SESSION_NOT_FOUND,
        .detail = "session_id and request_id are required",
    };
  }

  const std::lock_guard<std::mutex> lock(session_mutex_);
  if (stored_snapshot_.has_value()) {
    if (request.checkpoint_ref.has_value() &&
        stored_snapshot_->active_checkpoint_ref != request.checkpoint_ref) {
      return SessionLoadResult{
          .accepted = false,
          .created_new_session = false,
          .snapshot = std::nullopt,
          .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "requested checkpoint_ref does not match active session anchor",
      };
    }

    return make_session_load_result(
        *stored_snapshot_,
        false,
        "existing session loaded from in-memory store");
  }

  if (!request.allow_session_create) {
    return SessionLoadResult{
        .accepted = false,
        .created_new_session = false,
        .snapshot = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_400_SESSION_NOT_FOUND,
        .detail = "session does not exist and creation is disabled",
    };
  }

  return make_session_load_result(
      SessionSnapshot{
          .session_id = request.session_id,
          .request_id = request.request_id,
          .turn_index = 0,
          .active_checkpoint_ref = request.checkpoint_ref,
          .fsm_state = RuntimeState::Idle,
          .budget_snapshot_ref = std::nullopt,
          .pending_interaction = std::nullopt,
          .last_result_summary = std::nullopt,
      },
      true,
      "new session snapshot created for runtime-local store");
}

PrepareTurnResult SessionManager::prepare_turn(
    const PrepareTurnRequest& request) const {
  const auto& snapshot = request.session_snapshot;
  if (request.expected_checkpoint_ref.has_value() &&
      snapshot.active_checkpoint_ref != request.expected_checkpoint_ref) {
    return PrepareTurnResult{
        .accepted = false,
        .effective_session = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = "prepare_turn expected checkpoint_ref does not match session snapshot",
    };
  }

  if (snapshot.pending_interaction.has_value() && snapshot.pending_interaction->active() &&
      snapshot.fsm_state != RuntimeState::WaitingClarify &&
      snapshot.fsm_state != RuntimeState::WaitingConfirm &&
      snapshot.fsm_state != RuntimeState::WaitingExternal) {
    return PrepareTurnResult{
        .accepted = false,
        .effective_session = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = "pending interaction requires a waiting-state session snapshot",
    };
  }

  return make_prepare_turn_result(
      snapshot,
      request.resume_turn ? "resume turn prepared" : "fresh turn prepared");
}

SessionPersistResult SessionManager::persist_turn(
    const SessionPersistRequest& request) {
  if (!request.has_minimum_requirements() || request.terminal_state == RuntimeState::Idle) {
    return SessionPersistResult{
        .persisted = false,
        .active_checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = "persist_turn requires session identity and a non-idle terminal state",
    };
  }

  const std::lock_guard<std::mutex> lock(session_mutex_);
  stored_snapshot_ = request.session_snapshot;
  stored_snapshot_->fsm_state = request.terminal_state;
  if (request.checkpoint_ref.has_value()) {
    stored_snapshot_->active_checkpoint_ref = request.checkpoint_ref;
  }

  return make_session_persist_result(
      stored_snapshot_->active_checkpoint_ref,
      "turn persisted in in-memory session store");
}

SessionPersistResult SessionManager::bind_checkpoint_ref(
    const BindCheckpointRefRequest& request) {
  if (!request.has_minimum_requirements()) {
    return SessionPersistResult{
        .persisted = false,
        .active_checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = "bind_checkpoint_ref requires session_id, request_id and checkpoint_ref",
    };
  }

  const std::lock_guard<std::mutex> lock(session_mutex_);
  if (!stored_snapshot_.has_value()) {
    stored_snapshot_ = SessionSnapshot{
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

  return make_session_persist_result(
      stored_snapshot_->active_checkpoint_ref,
      "checkpoint reference bound to in-memory session");
}

ResumeSeedResult SessionManager::build_resume_seed(
    const BuildResumeSeedRequest& request) const {
  if (!request.has_minimum_requirements()) {
    return ResumeSeedResult{
        .resume_seed = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = "build_resume_seed requires session identity, checkpoint_ref and resume_reason",
    };
  }

  if (request.session_snapshot.active_checkpoint_ref.has_value() &&
      request.session_snapshot.active_checkpoint_ref != request.checkpoint_ref) {
    return ResumeSeedResult{
        .resume_seed = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = "resume seed checkpoint_ref must match active session anchor",
    };
  }

  return make_resume_seed_result(
      ResumeSeed{
          .session_id = request.session_snapshot.session_id,
          .request_id = request.session_snapshot.request_id,
          .checkpoint_ref = request.checkpoint_ref,
          .fsm_state = request.session_snapshot.fsm_state,
          .pending_interaction = request.session_snapshot.pending_interaction,
          .policy_snapshot_ref = request.policy_snapshot_ref,
          .resume_reason = request.resume_reason,
      },
      "resume seed built from in-memory session snapshot");
}

}  // namespace dasall::runtime
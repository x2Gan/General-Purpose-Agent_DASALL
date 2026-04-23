#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "RuntimeErrorCode.h"
#include "fsm/StateTransitionTypes.h"

namespace dasall::runtime {

enum class PendingInteractionKind : std::uint8_t {
  None = 0,
  Clarify,
  Confirm,
  WaitExternal,
};

[[nodiscard]] constexpr const char* pending_interaction_kind_name(
    const PendingInteractionKind kind) {
  switch (kind) {
    case PendingInteractionKind::None:
      return "None";
    case PendingInteractionKind::Clarify:
      return "Clarify";
    case PendingInteractionKind::Confirm:
      return "Confirm";
    case PendingInteractionKind::WaitExternal:
      return "WaitExternal";
  }

  return "Unknown";
}

struct PendingInteractionState {
  PendingInteractionKind interaction_kind = PendingInteractionKind::None;
  std::string prompt_token;
  std::optional<std::int64_t> deadline_ms;
  std::string blocking_reason;
  std::string resume_channel;
  std::string input_schema_hint;

  [[nodiscard]] bool active() const {
    return interaction_kind != PendingInteractionKind::None;
  }
};

struct SessionSnapshot {
  std::string session_id;
  std::string request_id;
  std::uint32_t turn_index = 0;
  std::optional<std::string> active_checkpoint_ref;
  RuntimeState fsm_state = RuntimeState::Idle;
  std::optional<std::string> budget_snapshot_ref;
  std::optional<PendingInteractionState> pending_interaction;
  std::optional<std::string> last_result_summary;

  [[nodiscard]] bool has_active_checkpoint() const {
    return active_checkpoint_ref.has_value() && !active_checkpoint_ref->empty();
  }
};

struct ResumeSeed {
  std::string session_id;
  std::string request_id;
  std::string checkpoint_ref;
  std::string resume_token;
  RuntimeState fsm_state = RuntimeState::Idle;
  std::optional<PendingInteractionState> pending_interaction;
  std::optional<std::string> policy_snapshot_ref;
  std::string resume_reason;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !session_id.empty() && !request_id.empty() && !checkpoint_ref.empty() &&
           !resume_token.empty() && !resume_reason.empty();
  }
};

struct SessionLoadRequest {
  std::string session_id;
  std::string request_id;
  std::optional<std::string> checkpoint_ref;
  bool allow_session_create = true;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !session_id.empty() && !request_id.empty();
  }
};

struct SessionLoadResult {
  bool accepted = false;
  bool created_new_session = false;
  std::optional<SessionSnapshot> snapshot;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool has_snapshot() const {
    return accepted && snapshot.has_value();
  }
};

struct PrepareTurnRequest {
  SessionSnapshot session_snapshot;
  bool resume_turn = false;
  std::optional<std::string> expected_checkpoint_ref;
};

struct PrepareTurnResult {
  bool accepted = false;
  std::optional<SessionSnapshot> effective_session;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;
};

struct SessionPersistRequest {
  SessionSnapshot session_snapshot;
  RuntimeState terminal_state = RuntimeState::Idle;
  std::optional<std::string> checkpoint_ref;
  std::optional<std::string> audit_summary;
  std::optional<std::string> next_resume_seed_ref;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !session_snapshot.session_id.empty() && !session_snapshot.request_id.empty();
  }
};

struct SessionPersistResult {
  bool persisted = false;
  std::optional<std::string> active_checkpoint_ref;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;
};

struct BindCheckpointRefRequest {
  std::string session_id;
  std::string request_id;
  std::string checkpoint_ref;
  RuntimeState fsm_state = RuntimeState::Idle;
  std::optional<PendingInteractionState> pending_interaction;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !session_id.empty() && !request_id.empty() && !checkpoint_ref.empty();
  }
};

struct BuildResumeSeedRequest {
  SessionSnapshot session_snapshot;
  std::string checkpoint_ref;
  std::string resume_token;
  std::string resume_reason;
  std::optional<std::string> policy_snapshot_ref;

  [[nodiscard]] bool has_minimum_requirements() const {
    return !session_snapshot.session_id.empty() && !session_snapshot.request_id.empty() &&
           !checkpoint_ref.empty() && !resume_token.empty() && !resume_reason.empty();
  }
};

struct ResumeSeedResult {
  std::optional<ResumeSeed> resume_seed;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool built() const {
    return resume_seed.has_value();
  }
};

[[nodiscard]] inline SessionLoadResult make_session_load_result(
    const SessionSnapshot& snapshot,
    const bool created_new_session,
    const std::string& detail = std::string()) {
  return SessionLoadResult{
      .accepted = true,
      .created_new_session = created_new_session,
      .snapshot = snapshot,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline PrepareTurnResult make_prepare_turn_result(
    const SessionSnapshot& effective_session,
    const std::string& detail = std::string()) {
  return PrepareTurnResult{
      .accepted = true,
      .effective_session = effective_session,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline SessionPersistResult make_session_persist_result(
    const std::optional<std::string>& active_checkpoint_ref,
    const std::string& detail = std::string()) {
  return SessionPersistResult{
      .persisted = true,
      .active_checkpoint_ref = active_checkpoint_ref,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

[[nodiscard]] inline ResumeSeedResult make_resume_seed_result(
    const ResumeSeed& resume_seed,
    const std::string& detail = std::string()) {
  return ResumeSeedResult{
      .resume_seed = resume_seed,
      .error_code = std::nullopt,
      .detail = detail,
  };
}

}  // namespace dasall::runtime
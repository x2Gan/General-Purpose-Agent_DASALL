#include "SessionManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>

namespace {

[[nodiscard]] std::string hex_encode(std::string_view value) {
  static constexpr char kHexDigits[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(value.size() * 2U);
  for (const unsigned char ch : value) {
    encoded.push_back(kHexDigits[(ch >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[ch & 0x0FU]);
  }
  return encoded;
}

[[nodiscard]] int hex_value(const char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

[[nodiscard]] bool hex_decode(std::string_view value, std::string& decoded) {
  if ((value.size() % 2U) != 0U) {
    return false;
  }

  decoded.clear();
  decoded.reserve(value.size() / 2U);
  for (std::size_t index = 0; index < value.size(); index += 2U) {
    const auto high = hex_value(value[index]);
    const auto low = hex_value(value[index + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    decoded.push_back(static_cast<char>((high << 4U) | low));
  }
  return true;
}

void write_field(std::ostream& output,
                 const std::string_view key,
                 const std::string_view value) {
  output << key << '=' << value << '\n';
}

template <typename Integer>
void write_integer_field(std::ostream& output,
                         const std::string_view key,
                         const Integer value) {
  output << key << '=' << value << '\n';
}

void write_encoded_field(std::ostream& output,
                         const std::string_view key,
                         const std::string& value) {
  write_field(output, key, hex_encode(value));
}

void write_optional_encoded_field(std::ostream& output,
                                  const std::string_view key,
                                  const std::optional<std::string>& value) {
  if (value.has_value()) {
    write_encoded_field(output, key, *value);
  }
}

template <typename Integer>
void write_optional_integer_field(std::ostream& output,
                                  const std::string_view key,
                                  const std::optional<Integer>& value) {
  if (value.has_value()) {
    write_integer_field(output, key, *value);
  }
}

[[nodiscard]] std::filesystem::path session_record_path(
    const std::string_view state_root,
    const std::string_view session_id) {
  return std::filesystem::path(std::string(state_root)) /
         "sessions" /
         (hex_encode(session_id) + ".kv");
}

[[nodiscard]] bool parse_key_value_file(
    std::istream& input,
    std::unordered_map<std::string, std::string>& fields,
    std::string& detail) {
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }

    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      detail = "durable session record contains an invalid key-value line";
      return false;
    }

    fields.emplace(line.substr(0, separator), line.substr(separator + 1U));
  }

  detail.clear();
  return true;
}

[[nodiscard]] bool decode_optional_string_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    std::optional<std::string>& target,
    std::string& detail) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end()) {
    target = std::nullopt;
    return true;
  }

  std::string decoded;
  if (!hex_decode(iterator->second, decoded)) {
    detail = std::string("durable session record could not decode ") + std::string(key);
    return false;
  }

  target = std::move(decoded);
  return true;
}

[[nodiscard]] bool decode_required_string_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    std::string& target,
    std::string& detail) {
  std::optional<std::string> decoded;
  if (!decode_optional_string_field(fields, key, decoded, detail)) {
    return false;
  }
  if (!decoded.has_value()) {
    detail = std::string("durable session record is missing ") + std::string(key);
    return false;
  }

  target = std::move(*decoded);
  return true;
}

template <typename Integer>
[[nodiscard]] bool decode_optional_integer_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    std::optional<Integer>& target,
    std::string& detail) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end()) {
    target = std::nullopt;
    return true;
  }

  try {
    if constexpr (std::is_same_v<Integer, std::uint32_t>) {
      target = static_cast<std::uint32_t>(std::stoul(iterator->second));
    } else if constexpr (std::is_same_v<Integer, std::int64_t>) {
      target = static_cast<std::int64_t>(std::stoll(iterator->second));
    } else {
      static_assert(sizeof(Integer) == 0U, "unsupported integer type");
    }
  } catch (const std::exception&) {
    detail = std::string("durable session record has an invalid integer for ") +
             std::string(key);
    return false;
  }

  return true;
}

template <typename Integer>
[[nodiscard]] bool decode_required_integer_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    Integer& target,
    std::string& detail) {
  std::optional<Integer> decoded;
  if (!decode_optional_integer_field(fields, key, decoded, detail)) {
    return false;
  }
  if (!decoded.has_value()) {
    detail = std::string("durable session record is missing ") + std::string(key);
    return false;
  }

  target = *decoded;
  return true;
}

[[nodiscard]] bool write_durable_session_record(
    const std::string_view state_root,
    const dasall::runtime::SessionSnapshot& session_snapshot,
    std::string& detail) {
  const auto record_path = session_record_path(state_root, session_snapshot.session_id);
  const auto temporary_path = record_path.string() + ".tmp";
  std::error_code error_code;
  std::filesystem::create_directories(record_path.parent_path(), error_code);
  if (error_code) {
    detail = "session durable store could not create state directories";
    return false;
  }

  {
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      detail = "session durable store could not open the session record for writing";
      return false;
    }

    write_encoded_field(output, "session_id", session_snapshot.session_id);
    write_encoded_field(output, "request_id", session_snapshot.request_id);
    write_integer_field(output, "turn_index", session_snapshot.turn_index);
    write_optional_encoded_field(output,
                                 "active_checkpoint_ref",
                                 session_snapshot.active_checkpoint_ref);
    write_integer_field(output, "fsm_state", static_cast<int>(session_snapshot.fsm_state));
    write_optional_encoded_field(output,
                                 "budget_snapshot_ref",
                                 session_snapshot.budget_snapshot_ref);
    write_optional_encoded_field(output,
                                 "last_result_summary",
                                 session_snapshot.last_result_summary);

    write_integer_field(output,
                        "pending_interaction_present",
                        session_snapshot.pending_interaction.has_value() ? 1 : 0);
    if (session_snapshot.pending_interaction.has_value()) {
      const auto& interaction = *session_snapshot.pending_interaction;
      write_integer_field(output,
                          "pending_interaction.kind",
                          static_cast<int>(interaction.interaction_kind));
      write_encoded_field(output, "pending_interaction.prompt_token", interaction.prompt_token);
      write_optional_integer_field(output,
                                   "pending_interaction.deadline_ms",
                                   interaction.deadline_ms);
      write_encoded_field(output,
                          "pending_interaction.blocking_reason",
                          interaction.blocking_reason);
      write_encoded_field(output,
                          "pending_interaction.resume_channel",
                          interaction.resume_channel);
      write_encoded_field(output,
                          "pending_interaction.input_schema_hint",
                          interaction.input_schema_hint);
    }
  }

  std::filesystem::rename(temporary_path, record_path, error_code);
  if (error_code) {
    detail = "session durable store could not atomically publish the session record";
    (void)std::filesystem::remove(temporary_path);
    return false;
  }

  detail = "session persisted in durable store";
  return true;
}

[[nodiscard]] bool read_durable_session_record(
    const std::string_view state_root,
    const std::string_view session_id,
    dasall::runtime::SessionSnapshot& session_snapshot,
    std::string& detail) {
  const auto record_path = session_record_path(state_root, session_id);
  if (!std::filesystem::exists(record_path)) {
    detail = "session not found in durable store";
    return false;
  }

  std::ifstream input(record_path, std::ios::binary);
  if (!input.is_open()) {
    detail = "session durable store could not open the session record";
    return false;
  }

  std::unordered_map<std::string, std::string> fields;
  if (!parse_key_value_file(input, fields, detail)) {
    return false;
  }

  if (!decode_required_string_field(fields, "session_id", session_snapshot.session_id, detail) ||
      !decode_required_string_field(fields, "request_id", session_snapshot.request_id, detail) ||
      !decode_required_integer_field(fields, "turn_index", session_snapshot.turn_index, detail) ||
      !decode_optional_string_field(fields,
                                    "active_checkpoint_ref",
                                    session_snapshot.active_checkpoint_ref,
                                    detail) ||
      !decode_optional_string_field(fields,
                                    "budget_snapshot_ref",
                                    session_snapshot.budget_snapshot_ref,
                                    detail) ||
      !decode_optional_string_field(fields,
                                    "last_result_summary",
                                    session_snapshot.last_result_summary,
                                    detail)) {
    return false;
  }

  std::uint32_t raw_fsm_state = 0;
  if (!decode_required_integer_field(fields, "fsm_state", raw_fsm_state, detail)) {
    return false;
  }
  session_snapshot.fsm_state = static_cast<dasall::runtime::RuntimeState>(raw_fsm_state);

  std::uint32_t pending_interaction_present = 0;
  if (!decode_required_integer_field(fields,
                                     "pending_interaction_present",
                                     pending_interaction_present,
                                     detail)) {
    return false;
  }

  if (pending_interaction_present == 1U) {
    dasall::runtime::PendingInteractionState interaction;
    std::uint32_t raw_kind = 0;
    if (!decode_required_integer_field(fields,
                                       "pending_interaction.kind",
                                       raw_kind,
                                       detail) ||
        !decode_required_string_field(fields,
                                      "pending_interaction.prompt_token",
                                      interaction.prompt_token,
                                      detail) ||
        !decode_optional_integer_field(fields,
                                       "pending_interaction.deadline_ms",
                                       interaction.deadline_ms,
                                       detail) ||
        !decode_required_string_field(fields,
                                      "pending_interaction.blocking_reason",
                                      interaction.blocking_reason,
                                      detail) ||
        !decode_required_string_field(fields,
                                      "pending_interaction.resume_channel",
                                      interaction.resume_channel,
                                      detail) ||
        !decode_required_string_field(fields,
                                      "pending_interaction.input_schema_hint",
                                      interaction.input_schema_hint,
                                      detail)) {
      return false;
    }

    interaction.interaction_kind =
        static_cast<dasall::runtime::PendingInteractionKind>(raw_kind);
    session_snapshot.pending_interaction = std::move(interaction);
  } else {
    session_snapshot.pending_interaction = std::nullopt;
  }

  if (session_snapshot.session_id != session_id) {
    detail = "durable session record session_id does not match file anchor";
    return false;
  }

  detail = "existing session loaded from durable store";
  return true;
}

}  // namespace

namespace dasall::runtime {

void SessionManager::set_durable_state_root(
    const std::optional<std::string>& state_root) {
  const std::lock_guard<std::mutex> lock(session_mutex_);
  if (state_root.has_value() && !state_root->empty()) {
    durable_state_root_ = state_root;
    return;
  }

  durable_state_root_ = std::nullopt;
}

void SessionManager::seed_for_test(const SessionSnapshot& session_snapshot) {
  const std::lock_guard<std::mutex> lock(session_mutex_);
  stored_snapshots_[session_snapshot.session_id] = session_snapshot;
  if (durable_state_root_.has_value()) {
    std::string detail;
    (void)write_durable_session_record(*durable_state_root_, session_snapshot, detail);
  }
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
  auto iterator = stored_snapshots_.find(request.session_id);
  if (iterator == stored_snapshots_.end() && durable_state_root_.has_value()) {
    SessionSnapshot durable_snapshot;
    std::string durable_detail;
    if (read_durable_session_record(
            *durable_state_root_, request.session_id, durable_snapshot, durable_detail)) {
      iterator = stored_snapshots_.emplace(request.session_id, std::move(durable_snapshot)).first;
    }
  }

  if (iterator != stored_snapshots_.end()) {
    const auto& snapshot = iterator->second;
    if (request.checkpoint_ref.has_value() &&
        snapshot.active_checkpoint_ref != request.checkpoint_ref) {
      return SessionLoadResult{
          .accepted = false,
          .created_new_session = false,
          .snapshot = std::nullopt,
          .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
          .detail = "requested checkpoint_ref does not match active session anchor",
      };
    }

    return make_session_load_result(
        snapshot,
        false,
        durable_state_root_.has_value() ? "existing session loaded from durable store cache"
                                        : "existing session loaded from in-memory store");
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
  auto& stored_snapshot = stored_snapshots_[request.session_snapshot.session_id];
  stored_snapshot = request.session_snapshot;
  stored_snapshot.fsm_state = request.terminal_state;
  if (request.checkpoint_ref.has_value()) {
    stored_snapshot.active_checkpoint_ref = request.checkpoint_ref;
  }

  std::string persist_detail = "turn persisted in in-memory session store";
  if (durable_state_root_.has_value() &&
      !write_durable_session_record(*durable_state_root_, stored_snapshot, persist_detail)) {
    return SessionPersistResult{
        .persisted = false,
        .active_checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = persist_detail,
    };
  }

  return make_session_persist_result(
      stored_snapshot.active_checkpoint_ref,
      persist_detail);
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
  auto iterator = stored_snapshots_.find(request.session_id);
  if (iterator == stored_snapshots_.end()) {
    iterator = stored_snapshots_.emplace(
                   request.session_id,
                   SessionSnapshot{
        .session_id = request.session_id,
        .request_id = request.request_id,
        .turn_index = 0,
        .active_checkpoint_ref = request.checkpoint_ref,
        .fsm_state = request.fsm_state,
        .budget_snapshot_ref = std::nullopt,
        .pending_interaction = request.pending_interaction,
        .last_result_summary = std::nullopt,
                   })
                   .first;
  } else {
    iterator->second.session_id = request.session_id;
    iterator->second.request_id = request.request_id;
    iterator->second.active_checkpoint_ref = request.checkpoint_ref;
    iterator->second.fsm_state = request.fsm_state;
    iterator->second.pending_interaction = request.pending_interaction;
  }

  std::string persist_detail = "checkpoint reference bound to in-memory session";
  if (durable_state_root_.has_value() &&
      !write_durable_session_record(*durable_state_root_, iterator->second, persist_detail)) {
    return SessionPersistResult{
        .persisted = false,
        .active_checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_401_SESSION_INCONSISTENT,
        .detail = persist_detail,
    };
  }

  return make_session_persist_result(
      iterator->second.active_checkpoint_ref,
      persist_detail);
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
        .resume_token = request.resume_token,
          .fsm_state = request.session_snapshot.fsm_state,
          .pending_interaction = request.session_snapshot.pending_interaction,
          .policy_snapshot_ref = request.policy_snapshot_ref,
          .resume_reason = request.resume_reason,
      },
      "resume seed built from in-memory session snapshot");
}

}  // namespace dasall::runtime
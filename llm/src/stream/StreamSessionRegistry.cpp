#include "stream/StreamSessionRegistry.h"

#include <algorithm>
#include <chrono>

namespace {

std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

namespace dasall::llm::stream {

bool StreamSessionRegistryConfig::has_consistent_values() const {
  return max_active_sessions > 0U && max_buffered_chars > 0U && session_ttl_ms > 0U;
}

bool StreamSessionSnapshot::has_consistent_values() const {
  return !session_id.empty() && !route_key.empty() && expires_at_ms > 0;
}

bool StreamSessionSnapshot::is_terminal() const {
  return state == StreamSessionState::Completed ||
         state == StreamSessionState::Cancelled ||
         state == StreamSessionState::Failed ||
         state == StreamSessionState::Expired;
}

bool StreamSessionMutationResult::is_terminal() const {
  return state == StreamSessionState::Completed ||
         state == StreamSessionState::Cancelled ||
         state == StreamSessionState::Failed ||
         state == StreamSessionState::Expired;
}

StreamSessionSnapshot StreamSessionRegistry::StreamSessionRecord::to_snapshot(
    std::string session_id) const {
  return StreamSessionSnapshot{
      .session_id = std::move(session_id),
      .route_key = route_key,
      .state = state,
      .buffered_chars = buffered_chars,
      .expires_at_ms = expires_at_ms,
  };
}

bool StreamSessionRegistry::init(StreamSessionRegistryConfig config) {
  if (!config.has_consistent_values()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
  sessions_.clear();
  initialized_ = true;
  return true;
}

StreamSessionMutationResult StreamSessionRegistry::accept(const StreamSessionRef& session_ref,
                                                          std::string_view route_key) {
  if (!initialized_ || session_ref.session_id.empty() || route_key.empty()) {
    return make_result(StreamSessionMutationStatus::InvalidState,
                       StreamSessionState::Accepted,
                       false);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = sessions_.find(session_ref.session_id);
  if (existing != sessions_.end()) {
    return make_result(StreamSessionMutationStatus::InvalidState,
                       existing->second.state,
                       false);
  }

  const auto active_sessions = std::count_if(sessions_.begin(), sessions_.end(),
                                             [&](const auto& entry) {
                                               return !is_terminal_locked(entry.second.state);
                                             });
  if (active_sessions >= static_cast<std::ptrdiff_t>(config_.max_active_sessions)) {
    return make_result(StreamSessionMutationStatus::CapacityExceeded,
                       StreamSessionState::Accepted,
                       false);
  }

  sessions_.emplace(session_ref.session_id,
                    StreamSessionRecord{
                        .route_key = std::string(route_key),
                        .state = StreamSessionState::Accepted,
                        .buffered_chars = 0U,
                        .expires_at_ms = current_time_ms() +
                                         static_cast<std::int64_t>(config_.session_ttl_ms),
                    });
  return make_result(StreamSessionMutationStatus::Ok, StreamSessionState::Accepted, true);
}

StreamSessionMutationResult StreamSessionRegistry::mark_active(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  switch (it->second.state) {
    case StreamSessionState::Accepted:
    case StreamSessionState::Active:
      it->second.state = StreamSessionState::Active;
      return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
    case StreamSessionState::CancelRequested:
      return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
    default:
      return make_result(StreamSessionMutationStatus::InvalidState, it->second.state, false);
  }
}

StreamSessionMutationResult StreamSessionRegistry::append_delta(std::string_view session_id,
                                                                std::uint32_t delta_chars) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  if (it->second.state == StreamSessionState::Accepted) {
    it->second.state = StreamSessionState::Active;
  }

  if (it->second.state == StreamSessionState::CancelRequested) {
    return make_result(StreamSessionMutationStatus::InvalidState, it->second.state, false);
  }

  if (it->second.state != StreamSessionState::Active) {
    return make_result(StreamSessionMutationStatus::InvalidState, it->second.state, false);
  }

  if (delta_chars > config_.max_buffered_chars ||
      it->second.buffered_chars > config_.max_buffered_chars - delta_chars) {
    it->second.state = StreamSessionState::Failed;
    return make_result(StreamSessionMutationStatus::Overflow, it->second.state, false);
  }

  it->second.buffered_chars += delta_chars;
  return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
}

StreamSessionMutationResult StreamSessionRegistry::request_cancel(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  if (is_terminal_locked(it->second.state)) {
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  it->second.state = StreamSessionState::CancelRequested;
  return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
}

StreamSessionMutationResult StreamSessionRegistry::mark_completing(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  switch (it->second.state) {
    case StreamSessionState::Accepted:
    case StreamSessionState::Active:
    case StreamSessionState::Completing:
      it->second.state = StreamSessionState::Completing;
      return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
    case StreamSessionState::CancelRequested:
      return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
    default:
      return make_result(StreamSessionMutationStatus::InvalidState, it->second.state, false);
  }
}

StreamSessionMutationResult StreamSessionRegistry::mark_completed(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  if (it->second.state == StreamSessionState::CancelRequested) {
    it->second.state = StreamSessionState::Cancelled;
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  if (is_terminal_locked(it->second.state)) {
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  switch (it->second.state) {
    case StreamSessionState::Accepted:
    case StreamSessionState::Active:
    case StreamSessionState::Completing:
      it->second.state = StreamSessionState::Completed;
      return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
    default:
      return make_result(StreamSessionMutationStatus::InvalidState, it->second.state, false);
  }
}

StreamSessionMutationResult StreamSessionRegistry::mark_failed(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  if (it->second.state == StreamSessionState::CancelRequested) {
    it->second.state = StreamSessionState::Cancelled;
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  if (is_terminal_locked(it->second.state)) {
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  switch (it->second.state) {
    case StreamSessionState::Accepted:
    case StreamSessionState::Active:
    case StreamSessionState::Completing:
      it->second.state = StreamSessionState::Failed;
      return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
    default:
      return make_result(StreamSessionMutationStatus::InvalidState, it->second.state, false);
  }
}

StreamSessionMutationResult StreamSessionRegistry::mark_expired(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return find_result(session_id);
  }

  if (it->second.state == StreamSessionState::CancelRequested) {
    it->second.state = StreamSessionState::Cancelled;
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  if (is_terminal_locked(it->second.state)) {
    return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
  }

  it->second.state = StreamSessionState::Expired;
  return make_result(StreamSessionMutationStatus::Ok, it->second.state, true);
}

bool StreamSessionRegistry::cleanup(std::string_view session_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return true;
  }

  if (!is_terminal_locked(it->second.state)) {
    return false;
  }

  sessions_.erase(it);
  return true;
}

std::optional<StreamSessionSnapshot> StreamSessionRegistry::find(
    std::string_view session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = sessions_.find(std::string(session_id));
  if (it == sessions_.end()) {
    return std::nullopt;
  }

  return it->second.to_snapshot(it->first);
}

std::vector<StreamSessionSnapshot> StreamSessionRegistry::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StreamSessionSnapshot> entries;
  entries.reserve(sessions_.size());
  for (const auto& [session_id, record] : sessions_) {
    entries.push_back(record.to_snapshot(session_id));
  }

  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return left.session_id < right.session_id;
  });
  return entries;
}

std::uint32_t StreamSessionRegistry::active_session_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(std::count_if(sessions_.begin(), sessions_.end(),
                                                  [&](const auto& entry) {
                                                    return !is_terminal_locked(entry.second.state);
                                                  }));
}

bool StreamSessionRegistry::reap_expired(std::int64_t now_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool changed = false;
  for (auto& [session_id, record] : sessions_) {
    static_cast<void>(session_id);
    if (!is_terminal_locked(record.state) && record.expires_at_ms <= now_ms) {
      record.state = StreamSessionState::Expired;
      changed = true;
    }
  }

  return changed;
}

StreamSessionMutationResult StreamSessionRegistry::make_result(
    StreamSessionMutationStatus status,
    StreamSessionState state,
    bool ok) const {
  return StreamSessionMutationResult{
      .ok = ok,
      .status = status,
      .state = state,
  };
}

StreamSessionMutationResult StreamSessionRegistry::find_result(std::string_view) const {
  return make_result(StreamSessionMutationStatus::NotFound,
                     StreamSessionState::Accepted,
                     false);
}

bool StreamSessionRegistry::is_terminal_locked(StreamSessionState state) const {
  return state == StreamSessionState::Completed ||
         state == StreamSessionState::Cancelled ||
         state == StreamSessionState::Failed ||
         state == StreamSessionState::Expired;
}

}  // namespace dasall::llm::stream
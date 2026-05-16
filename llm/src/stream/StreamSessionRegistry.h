#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "stream/StreamSessionRef.h"

namespace dasall::llm::stream {

enum class StreamSessionState {
  Accepted = 0,
  Active = 1,
  Completing = 2,
  Completed = 3,
  CancelRequested = 4,
  Cancelled = 5,
  Failed = 6,
  Expired = 7,
};

struct StreamSessionRegistryConfig {
  std::uint32_t max_active_sessions = 8U;
  std::uint32_t max_buffered_chars = 32768U;
  std::uint32_t session_ttl_ms = 60000U;

  [[nodiscard]] bool has_consistent_values() const;
};

struct StreamSessionSnapshot {
  std::string session_id;
  std::string route_key;
  StreamSessionState state = StreamSessionState::Accepted;
  std::uint32_t buffered_chars = 0U;
  std::int64_t expires_at_ms = 0;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] bool is_terminal() const;
};

enum class StreamSessionMutationStatus {
  Ok = 0,
  NotFound = 1,
  CapacityExceeded = 2,
  InvalidState = 3,
  Overflow = 4,
};

struct StreamSessionMutationResult {
  bool ok = false;
  StreamSessionMutationStatus status = StreamSessionMutationStatus::InvalidState;
  StreamSessionState state = StreamSessionState::Accepted;

  [[nodiscard]] bool is_terminal() const;
};

class StreamSessionRegistry {
 public:
  bool init(StreamSessionRegistryConfig config = {});

  [[nodiscard]] StreamSessionMutationResult accept(const StreamSessionRef& session_ref,
                                                   std::string_view route_key);
  [[nodiscard]] StreamSessionMutationResult mark_active(std::string_view session_id);
  [[nodiscard]] StreamSessionMutationResult append_delta(std::string_view session_id,
                                                         std::uint32_t delta_chars);
  [[nodiscard]] StreamSessionMutationResult request_cancel(std::string_view session_id);
  [[nodiscard]] StreamSessionMutationResult mark_completing(std::string_view session_id);
  [[nodiscard]] StreamSessionMutationResult mark_completed(std::string_view session_id);
  [[nodiscard]] StreamSessionMutationResult mark_failed(std::string_view session_id);
  [[nodiscard]] StreamSessionMutationResult mark_expired(std::string_view session_id);

  bool cleanup(std::string_view session_id);
  [[nodiscard]] std::optional<StreamSessionSnapshot> find(std::string_view session_id) const;
  [[nodiscard]] std::vector<StreamSessionSnapshot> snapshot() const;
  [[nodiscard]] std::uint32_t active_session_count() const;
  bool reap_expired(std::int64_t now_ms);

 private:
  struct StreamSessionRecord {
    std::string route_key;
    StreamSessionState state = StreamSessionState::Accepted;
    std::uint32_t buffered_chars = 0U;
    std::int64_t expires_at_ms = 0;

    [[nodiscard]] StreamSessionSnapshot to_snapshot(std::string session_id) const;
  };

  [[nodiscard]] StreamSessionMutationResult make_result(StreamSessionMutationStatus status,
                                                        StreamSessionState state,
                                                        bool ok) const;
  [[nodiscard]] StreamSessionMutationResult find_result(std::string_view session_id) const;
  [[nodiscard]] bool is_terminal_locked(StreamSessionState state) const;

  StreamSessionRegistryConfig config_{};
  std::unordered_map<std::string, StreamSessionRecord> sessions_;
  bool initialized_ = false;
  mutable std::mutex mutex_;
};

}  // namespace dasall::llm::stream
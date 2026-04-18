#include "working/IWorkingMemoryBoard.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dasall::memory {
namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] bool is_expired(const WorkingMemorySlot& slot,
                              std::int64_t now_ms) {
  if (slot.ttl_ms <= 0 || slot.updated_at <= 0) {
    return false;
  }

  return now_ms - slot.updated_at >= slot.ttl_ms;
}

struct SessionWorkingMemoryState {
  std::vector<WorkingMemorySlot> slots;
  std::vector<std::string> open_questions;
  std::vector<std::string> ephemeral_facts;
};

class WorkingMemoryBoard final : public IWorkingMemoryBoard {
 public:
  explicit WorkingMemoryBoard(std::size_t max_slots_per_session)
      : max_slots_per_session_(std::max<std::size_t>(1, max_slots_per_session)) {}

  void set_slot(const std::string& session_id,
                const WorkingMemorySlot& slot) override {
    if (session_id.empty() || slot.key.empty()) {
      return;
    }

    std::unique_lock lock(mutex_);
    auto& state = sessions_[session_id];
    WorkingMemorySlot normalized = slot;
    if (normalized.updated_at <= 0) {
      normalized.updated_at = current_time_ms();
    }

    const auto existing = std::find_if(
        state.slots.begin(), state.slots.end(),
        [&normalized](const WorkingMemorySlot& candidate) {
          return candidate.key == normalized.key;
        });

    if (existing != state.slots.end()) {
      *existing = std::move(normalized);
    } else {
      state.slots.push_back(std::move(normalized));
    }

    evict_expired_locked(state, current_time_ms());
    trim_slots_locked(state);
  }

  [[nodiscard]] std::optional<WorkingMemorySlot> get_slot(
      const std::string& session_id,
      const std::string& key) const override {
    std::shared_lock lock(mutex_);
    const auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
      return std::nullopt;
    }

    const auto slot_it = std::find_if(
        session_it->second.slots.begin(), session_it->second.slots.end(),
        [&key](const WorkingMemorySlot& candidate) {
          return candidate.key == key;
        });

    if (slot_it == session_it->second.slots.end()) {
      return std::nullopt;
    }

    const auto now_ms = current_time_ms();
    if (is_expired(*slot_it, now_ms)) {
      return std::nullopt;
    }

    return *slot_it;
  }

  void remove_slot(const std::string& session_id,
                   const std::string& key) override {
    std::unique_lock lock(mutex_);
    const auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
      return;
    }

    auto& slots = session_it->second.slots;
    slots.erase(std::remove_if(slots.begin(), slots.end(),
                               [&key](const WorkingMemorySlot& slot) {
                                 return slot.key == key;
                               }),
                slots.end());

    erase_session_if_empty_locked(session_id, session_it->second);
  }

  void clear_session(const std::string& session_id) override {
    std::unique_lock lock(mutex_);
    sessions_.erase(session_id);
  }

  [[nodiscard]] WorkingMemorySnapshot export_snapshot(
      const std::string& session_id) const override {
    std::unique_lock lock(mutex_);
    WorkingMemorySnapshot snapshot;
    snapshot.session_id = session_id;
    snapshot.snapshot_at = current_time_ms();

    const auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
      return snapshot;
    }

    evict_expired_locked(session_it->second, snapshot.snapshot_at);
    snapshot.slots = session_it->second.slots;
    snapshot.open_questions = session_it->second.open_questions;
    snapshot.ephemeral_facts = session_it->second.ephemeral_facts;
    return snapshot;
  }

  void restore_snapshot(const WorkingMemorySnapshot& snapshot) override {
    if (snapshot.session_id.empty()) {
      return;
    }

    std::unique_lock lock(mutex_);
    auto& state = sessions_[snapshot.session_id];
    state = SessionWorkingMemoryState{};

    const auto now_ms = current_time_ms();
    for (const auto& slot : snapshot.slots) {
      if (slot.key.empty()) {
        continue;
      }

      WorkingMemorySlot restored_slot = slot;
      if (restored_slot.updated_at <= 0) {
        restored_slot.updated_at = now_ms;
      }

      const auto existing = std::find_if(
          state.slots.begin(), state.slots.end(),
          [&restored_slot](const WorkingMemorySlot& candidate) {
            return candidate.key == restored_slot.key;
          });

      if (existing != state.slots.end()) {
        *existing = std::move(restored_slot);
      } else {
        state.slots.push_back(std::move(restored_slot));
      }
    }

    state.open_questions = snapshot.open_questions;
    state.ephemeral_facts = snapshot.ephemeral_facts;
    evict_expired_locked(state, now_ms);
    trim_slots_locked(state);
    erase_session_if_empty_locked(snapshot.session_id, state);
  }

  void evict_expired(const std::string& session_id) override {
    std::unique_lock lock(mutex_);
    const auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
      return;
    }

    evict_expired_locked(session_it->second, current_time_ms());
    erase_session_if_empty_locked(session_id, session_it->second);
  }

 private:
  void evict_expired_locked(SessionWorkingMemoryState& state,
                            std::int64_t now_ms) const {
    state.slots.erase(std::remove_if(state.slots.begin(), state.slots.end(),
                                     [now_ms](const WorkingMemorySlot& slot) {
                                       return is_expired(slot, now_ms);
                                     }),
                      state.slots.end());
  }

  void trim_slots_locked(SessionWorkingMemoryState& state) const {
    while (state.slots.size() > max_slots_per_session_) {
      const auto candidate = std::min_element(
          state.slots.begin(), state.slots.end(),
          [](const WorkingMemorySlot& left, const WorkingMemorySlot& right) {
            const bool left_has_ttl = left.ttl_ms > 0;
            const bool right_has_ttl = right.ttl_ms > 0;
            if (left_has_ttl != right_has_ttl) {
              return left_has_ttl;
            }

            if (left.updated_at != right.updated_at) {
              return left.updated_at < right.updated_at;
            }

            return left.key < right.key;
          });

      state.slots.erase(candidate);
    }
  }

  void erase_session_if_empty_locked(const std::string& session_id,
                                     const SessionWorkingMemoryState& state) const {
    if (!state.slots.empty() || !state.open_questions.empty() ||
        !state.ephemeral_facts.empty()) {
      return;
    }

    sessions_.erase(session_id);
  }

  const std::size_t max_slots_per_session_;
  mutable std::unordered_map<std::string, SessionWorkingMemoryState> sessions_;
  mutable std::shared_mutex mutex_;
};

}  // namespace

std::unique_ptr<IWorkingMemoryBoard> create_working_memory_board(
    std::size_t max_slots_per_session) {
  return std::make_unique<WorkingMemoryBoard>(max_slots_per_session);
}

}  // namespace dasall::memory

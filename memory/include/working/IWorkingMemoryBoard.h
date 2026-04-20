#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "working/WorkingMemorySnapshot.h"

namespace dasall::memory {

/// In-memory working board for short-lived slot data scoped to sessions.
///
/// Thread-safety contract:
/// - All methods are safe to call from any thread. The concrete implementation
///   uses an internal mutex to protect the slot map.
/// - export_snapshot() and get_slot() are read-side and may run concurrently
///   with each other.
/// - set_slot(), remove_slot(), clear_session(), restore_snapshot(), and
///   evict_expired() are write-side and serialize against each other.
class IWorkingMemoryBoard {
 public:
  virtual ~IWorkingMemoryBoard() = default;

  virtual void set_slot(const std::string& session_id,
                        const WorkingMemorySlot& slot) = 0;

  [[nodiscard]] virtual std::optional<WorkingMemorySlot> get_slot(
      const std::string& session_id,
      const std::string& key) const = 0;

  virtual void remove_slot(const std::string& session_id,
                           const std::string& key) = 0;
  virtual void clear_session(const std::string& session_id) = 0;

  [[nodiscard]] virtual WorkingMemorySnapshot export_snapshot(
      const std::string& session_id) const = 0;

  virtual void restore_snapshot(const WorkingMemorySnapshot& snapshot) = 0;
  virtual void evict_expired(const std::string& session_id) = 0;
};

[[nodiscard]] std::unique_ptr<IWorkingMemoryBoard> create_working_memory_board(
    std::size_t max_slots_per_session = 128);

}  // namespace dasall::memory

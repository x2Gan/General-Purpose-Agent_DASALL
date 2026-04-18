#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dasall::memory {

struct WorkingMemorySlot {
  std::string key;
  std::string value;
  std::int64_t updated_at = 0;
  std::int64_t ttl_ms = 0;
  std::string source;
};

struct WorkingMemorySnapshot {
  std::string session_id;
  std::vector<WorkingMemorySlot> slots;
  std::vector<std::string> open_questions;
  std::vector<std::string> ephemeral_facts;
  std::int64_t snapshot_at = 0;
};

}  // namespace dasall::memory

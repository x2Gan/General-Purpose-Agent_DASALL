#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "memory/Session.h"
#include "memory/Turn.h"
#include "store/StoreResult.h"

namespace dasall::memory {

class IMemoryStore;

struct SessionLoadBundle {
  contracts::Session session;
  std::vector<contracts::Turn> recent_turns;
  int total_turn_count = 0;
};

struct SessionLoadRequest {
  std::string session_id;
  int recent_turn_limit = 10;
};

using ISessionStore = IMemoryStore;

}  // namespace dasall::memory
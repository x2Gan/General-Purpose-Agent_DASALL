#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "memory/Session.h"
#include "memory/Turn.h"
#include "store/StoreResult.h"

namespace dasall::memory {

struct SessionLoadBundle {
  contracts::Session session;
  std::vector<contracts::Turn> recent_turns;
  int total_turn_count = 0;
};

struct SessionLoadRequest {
  std::string session_id;
  int recent_turn_limit = 10;
};

class ISessionStore {
 public:
  virtual ~ISessionStore() = default;

  [[nodiscard]] virtual SessionLoadBundle load_session_bundle(
      const SessionLoadRequest& request) const = 0;
  [[nodiscard]] virtual StoreResult create_session(
      const contracts::Session& session) = 0;
  [[nodiscard]] virtual StoreResult append_turn(
      const contracts::Turn& turn) = 0;
  [[nodiscard]] virtual StoreResult update_session_active(
      const std::string& session_id, std::int64_t last_active_at) = 0;
};

}  // namespace dasall::memory
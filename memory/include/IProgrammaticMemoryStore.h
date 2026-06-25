#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "store/StoreResult.h"

namespace dasall::memory {

struct ProgrammaticMemoryRecord {
  std::string asset_ref;
  std::string session_id;
  std::string source_turn_id;
  std::string content_digest;
  std::int64_t lease_expires_at = 0;
  std::vector<std::string> tags;

  [[nodiscard]] bool has_consistent_values() const {
    return !asset_ref.empty() && !session_id.empty() && !source_turn_id.empty() &&
           !content_digest.empty() && lease_expires_at > 0;
  }
};

struct ProgrammaticMemoryQuery {
  std::string session_id;
  std::optional<std::string> asset_ref;
  int limit = 20;
};

struct ProgrammaticMemoryLease {
  std::string asset_ref;
  std::int64_t lease_expires_at = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return !asset_ref.empty() && lease_expires_at > 0;
  }
};

class IProgrammaticMemoryStore {
 public:
  virtual ~IProgrammaticMemoryStore() = default;

  [[nodiscard]] virtual std::vector<ProgrammaticMemoryRecord> query_programmatic_assets(
      const ProgrammaticMemoryQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult upsert_programmatic_asset(
      const ProgrammaticMemoryRecord& record) = 0;
  [[nodiscard]] virtual StoreResult renew_programmatic_asset_lease(
      const ProgrammaticMemoryLease& lease) = 0;
};

}  // namespace dasall::memory
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "memory/MemoryFact.h"
#include "store/StoreResult.h"

namespace dasall::memory {

struct FactQuery {
  std::optional<std::string> session_id;
  std::optional<std::string> user_id;
  std::optional<std::string> fact_type;
  std::optional<int> min_confidence;
  bool exclude_superseded = true;
  int limit = 50;
};

struct FactQueryResult {
  std::vector<contracts::MemoryFact> facts;
  int total_count = 0;
};

class IFactStore {
 public:
  virtual ~IFactStore() = default;

  [[nodiscard]] virtual FactQueryResult query_facts(const FactQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult insert_fact(
      const contracts::MemoryFact& fact) = 0;
  [[nodiscard]] virtual StoreResult supersede_fact(
      const std::string& old_fact_id, const std::string& new_fact_id) = 0;
};

}  // namespace dasall::memory
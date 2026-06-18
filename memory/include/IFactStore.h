#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
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
  std::unordered_map<std::string, double> decay_weight_by_fact_id;
  std::unordered_map<std::string, double> recency_score_by_fact_id;
  std::unordered_map<std::string, double> hit_rate_score_by_fact_id;
  int total_count = 0;
};

class IFactStore {
 public:
  virtual ~IFactStore() = default;

  [[nodiscard]] virtual FactQueryResult query_facts(const FactQuery& query) const = 0;
  [[nodiscard]] virtual FactQueryResult query_facts_by_user(
    const std::string& user_id,
    const FactQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult touch_facts(
      const std::vector<std::string>& fact_ids,
      std::int64_t accessed_at) = 0;
  [[nodiscard]] virtual StoreResult insert_fact(
      const contracts::MemoryFact& fact) = 0;
  [[nodiscard]] virtual StoreResult supersede_fact(
      const std::string& old_fact_id, const std::string& new_fact_id) = 0;
};

}  // namespace dasall::memory
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory/MemoryFact.h"
#include "store/StoreResult.h"

namespace dasall::memory {

class IMemoryStore;

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

using IFactStore = IMemoryStore;

}  // namespace dasall::memory
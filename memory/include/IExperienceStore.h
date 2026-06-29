#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory/ExperienceMemory.h"
#include "store/StoreResult.h"

namespace dasall::memory {

class IMemoryStore;

struct ExperienceQuery {
  std::optional<std::string> session_id;
  std::optional<std::string> user_id;
  std::optional<std::string> stage;
  std::optional<std::vector<std::string>> applicable_domains;
  bool exclude_expired = true;
  int limit = 20;
};

struct ExperienceQueryResult {
  std::vector<contracts::ExperienceMemory> experiences;
  std::unordered_map<std::string, double> decay_weight_by_experience_id;
  std::unordered_map<std::string, double> recency_score_by_experience_id;
  std::unordered_map<std::string, double> hit_rate_score_by_experience_id;
  int total_count = 0;
};

using IExperienceStore = IMemoryStore;

}  // namespace dasall::memory
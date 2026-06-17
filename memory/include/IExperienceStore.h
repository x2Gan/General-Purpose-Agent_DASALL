#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "memory/ExperienceMemory.h"
#include "store/StoreResult.h"

namespace dasall::memory {

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
  int total_count = 0;
};

class IExperienceStore {
 public:
  virtual ~IExperienceStore() = default;

  [[nodiscard]] virtual ExperienceQueryResult query_experiences(
      const ExperienceQuery& query) const = 0;
    [[nodiscard]] virtual StoreResult touch_experiences(
      const std::vector<std::string>& experience_ids,
      std::int64_t accessed_at) = 0;
  [[nodiscard]] virtual StoreResult insert_experience(
      const contracts::ExperienceMemory& experience) = 0;
};

}  // namespace dasall::memory
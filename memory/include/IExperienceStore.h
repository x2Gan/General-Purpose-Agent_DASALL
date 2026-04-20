#pragma once

#include <optional>
#include <string>
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
  int total_count = 0;
};

class IExperienceStore {
 public:
  virtual ~IExperienceStore() = default;

  [[nodiscard]] virtual ExperienceQueryResult query_experiences(
      const ExperienceQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult insert_experience(
      const contracts::ExperienceMemory& experience) = 0;
};

}  // namespace dasall::memory
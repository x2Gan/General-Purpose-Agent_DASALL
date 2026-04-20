#pragma once

#include <optional>
#include <string>
#include <vector>

#include "IFactStore.h"
#include "writeback/MemoryWritebackRequest.h"
#include "writeback/WritebackResult.h"

namespace dasall::memory {

struct ConflictResolutionPlan {
  ConflictAction action = ConflictAction::Accept;
  std::vector<ConflictRecord> conflict_records;
  std::optional<std::string> supersede_target_id;
  std::vector<std::string> warnings;
  bool degraded = false;
};

class MemoryConflictResolver {
 public:
  explicit MemoryConflictResolver(IFactStore& store);

  [[nodiscard]] ConflictResolutionPlan resolve(
      const FactCandidate& candidate,
      const std::string& session_id);

 private:
  [[nodiscard]] std::vector<contracts::MemoryFact> find_related_facts(
      const std::string& session_id,
      const std::optional<std::string>& fact_type,
      const std::string& fact_text);

  [[nodiscard]] bool is_semantically_conflicting(
      const contracts::MemoryFact& existing,
      const FactCandidate& candidate) const;

  [[nodiscard]] ConflictAction determine_action(
      const contracts::MemoryFact& existing,
      const FactCandidate& candidate) const;

  IFactStore& store_;
};

}  // namespace dasall::memory
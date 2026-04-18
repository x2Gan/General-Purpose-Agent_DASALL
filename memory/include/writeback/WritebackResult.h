#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error/ResultCode.h"

namespace dasall::memory {

enum class ConflictAction {
  Accept = 0,
  Supersede = 1,
  Reject = 2,
  Coexist = 3,
};

struct ConflictRecord {
  std::string new_fact_id;
  std::string existing_fact_id;
  ConflictAction action = ConflictAction::Accept;
  std::string reason;
  int confidence_delta = 0;
};

struct WritebackResult {
  std::optional<contracts::ResultCode> result_code;
  std::optional<std::string> persisted_turn_id;
  std::optional<std::string> summary_id;
  std::vector<std::string> fact_ids;
  std::vector<std::string> experience_ids;
  std::vector<ConflictRecord> conflicts;
  std::vector<std::string> warnings;
  bool degraded = false;
  bool partial = false;
  bool retryable_storage_failure = false;
};

}  // namespace dasall::memory
#pragma once

#include <string>
#include <vector>

#include "context/RetrievalEvidenceRef.h"

namespace dasall::memory {

struct MemoryContextRequest {
  std::string request_id;
  std::string session_id;
  std::string trace_id;
  std::string stage;
  std::string user_turn;
  std::string goal_summary;
  std::string constraints_summary;
  std::string latest_observation_digest_summary;
  std::vector<std::string> visible_tools;
  int token_budget_hint = 4096;
  int latency_budget_ms = 0;
  std::vector<std::string> external_evidence;
  std::vector<contracts::RetrievalEvidenceRef> retrieval_evidence_refs;
};

}  // namespace dasall::memory
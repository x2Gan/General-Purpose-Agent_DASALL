#pragma once

#include <memory>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "IMemoryStore.h"
#include "config/MemoryConfig.h"
#include "memory/SummaryMemory.h"
#include "vector/VectorMemoryIndexAdapter.h"
#include "working/IWorkingMemoryBoard.h"

namespace dasall::memory::util {

class ITokenEstimator;

}  // namespace dasall::memory::util

namespace dasall::memory {

struct CandidateCollectRequest {
  std::string session_id;
  std::string stage;
  std::string goal_summary;
  int token_budget_hint = 4096;
  int latency_budget_ms = 0;
  std::vector<std::string> external_evidence;
};

struct CandidateSet {
  WorkingMemorySnapshot working_snapshot;
  SessionLoadBundle session_bundle;
  std::optional<contracts::SummaryMemory> latest_summary;
  std::vector<contracts::MemoryFact> relevant_facts;
  std::vector<contracts::ExperienceMemory> relevant_experiences;
  std::vector<std::string> external_evidence;
  std::vector<VectorHit> vector_hits;
  std::vector<std::string> warnings;
  int estimated_total_tokens = 0;
};

class CandidateCollector {
 public:
  CandidateCollector(IWorkingMemoryBoard& working_memory_board,
                     IMemoryStore& store,
                     const MemoryConfig& config,
                     VectorMemoryIndexAdapter* vector_index = nullptr,
                     std::shared_ptr<const util::ITokenEstimator> token_estimator = nullptr,
                     std::shared_ptr<std::mutex> writer_mutex = nullptr);

  [[nodiscard]] CandidateSet collect(const CandidateCollectRequest& request);

 private:
  [[nodiscard]] SessionLoadBundle load_session_context(
      const std::string& session_id) const;
  [[nodiscard]] std::vector<contracts::MemoryFact> query_relevant_facts(
      const CandidateCollectRequest& request,
      const SessionLoadBundle& session_bundle) const;
  [[nodiscard]] std::vector<contracts::ExperienceMemory> query_relevant_experiences(
      const CandidateCollectRequest& request,
      const SessionLoadBundle& session_bundle) const;
    void touch_fact_access(const std::vector<contracts::MemoryFact>& facts,
               std::vector<std::string>& warnings) const;
    void touch_experience_access(
      const std::vector<contracts::ExperienceMemory>& experiences,
      std::vector<std::string>& warnings) const;
  [[nodiscard]] std::vector<VectorHit> search_vector(
      const CandidateCollectRequest& request) const;
  [[nodiscard]] int estimate_tokens(const CandidateSet& set) const;

  IWorkingMemoryBoard& working_memory_board_;
  IMemoryStore& store_;
  ContextConfig context_config_{};
  VectorConfig vector_config_{};
  VectorMemoryIndexAdapter* vector_index_ = nullptr;
  std::shared_ptr<const util::ITokenEstimator> token_estimator_;
  std::shared_ptr<std::mutex> writer_mutex_;
};

}  // namespace dasall::memory
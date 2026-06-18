#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ISessionStore.h"
#include "ISummaryStore.h"
#include "ISummarizer.h"
#include "config/MemoryConfig.h"
#include "writeback/CompressionCoordinator.h"
#include "writeback/HierarchicalSummaryRequest.h"

namespace dasall::memory::util {

class ITokenEstimator;

}  // namespace dasall::memory::util

namespace dasall::memory {

struct HierarchicalSummarizationResult {
  std::optional<contracts::SummaryMemory> promoted_summary;
  std::vector<std::string> warnings;
  bool promoted = false;
};

class HierarchicalSummarizationCoordinator {
 public:
  HierarchicalSummarizationCoordinator(
      ISessionStore& session_store,
      ISummaryStore& summary_store,
      const MemoryConfig& config,
      ISummarizer* summarizer = nullptr,
      std::shared_ptr<const util::ITokenEstimator> token_estimator = nullptr);

  [[nodiscard]] HierarchicalSummarizationResult promote_from_summary(
      const contracts::SummaryMemory& summary);

 private:
  [[nodiscard]] int threshold_for(HierarchicalSummaryLevel source_level) const;
  [[nodiscard]] std::optional<std::string> resolve_user_id(
      const std::string& session_id) const;
  [[nodiscard]] std::vector<contracts::Turn> build_turns_from_summaries(
      const HierarchicalSummaryRequest& request) const;
  [[nodiscard]] std::string build_parent_summary_id(
      const HierarchicalSummaryRequest& request) const;
  [[nodiscard]] int target_budget_for(
      HierarchicalSummaryLevel target_level) const;
  [[nodiscard]] HierarchicalSummarizationResult promote_once(
      const HierarchicalSummaryRequest& request);

  ISessionStore& session_store_;
  ISummaryStore& summary_store_;
  CompressionCoordinator compression_coordinator_;
    CompressionConfig::HierarchyConfig hierarchy_config_;
};

}  // namespace dasall::memory
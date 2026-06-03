#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ISummaryStore.h"
#include "ISummarizer.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"
#include "writeback/SummaryProjection.h"

namespace dasall::memory::util {

class ITokenEstimator;

}  // namespace dasall::memory::util

namespace dasall::memory {

struct CompressionInput {
  std::string session_id;
  std::vector<contracts::Turn> source_turns;
  std::optional<contracts::SummaryMemory> existing_summary;
  int target_token_budget = 0;
  bool materialize_latest_summary = false;
  std::string strategy_hint;
};

struct CompressionOutput {
  contracts::SummaryMemory summary;
  SummaryProjection projection;
  std::vector<std::string> compression_notes;
  bool compression_applied = false;
};

class CompressionCoordinator {
 public:
  explicit CompressionCoordinator(ISummaryStore& store,
                                  ISummarizer* summarizer = nullptr,
                                  std::shared_ptr<const util::ITokenEstimator> token_estimator = nullptr);

  [[nodiscard]] CompressionOutput compress(const CompressionInput& input);

 private:
  [[nodiscard]] SummaryProjection extract_structured_summary(
      const CompressionInput& input) const;
  [[nodiscard]] std::vector<std::string> extract_decisions(
      const std::vector<contracts::Turn>& turns) const;
  [[nodiscard]] std::vector<std::string> extract_confirmed_facts(
      const std::vector<contracts::Turn>& turns) const;
  [[nodiscard]] std::vector<std::string> extract_tool_outcomes(
      const std::vector<contracts::Turn>& turns) const;
  [[nodiscard]] std::string build_summary_text(
      const CompressionInput& input,
      const SummaryProjection& projection) const;
  [[nodiscard]] SummaryProjection merge_with_existing(
      const std::optional<contracts::SummaryMemory>& existing_summary,
      SummaryProjection projection) const;
  [[nodiscard]] contracts::SummaryMemory materialize_summary(
      const CompressionInput& input,
      const SummaryProjection& projection) const;

  ISummaryStore& store_;
  ISummarizer* summarizer_ = nullptr;
    std::shared_ptr<const util::ITokenEstimator> token_estimator_;
};

}  // namespace dasall::memory
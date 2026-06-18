#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "memory/SummaryMemory.h"
#include "store/StoreResult.h"
#include "writeback/HierarchicalSummaryRequest.h"

namespace dasall::memory {

class ISummaryStore {
 public:
  virtual ~ISummaryStore() = default;

  [[nodiscard]] virtual StoreResult upsert_summary(
      const contracts::SummaryMemory& summary) = 0;
  [[nodiscard]] virtual std::optional<contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const = 0;
  [[nodiscard]] virtual std::optional<contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id,
            HierarchicalSummaryLevel level) const {
        const auto summary = load_latest_summary(session_id);
        if (!summary.has_value() || !summary_matches_level(*summary, level)) {
            return std::nullopt;
        }
        return summary;
    }
  [[nodiscard]] virtual std::vector<contracts::SummaryMemory>
  load_unparented_summaries(const std::string& session_id,
                            HierarchicalSummaryLevel level,
                                                        std::size_t limit) const {
        (void)session_id;
        (void)level;
        (void)limit;
        return {};
    }
  [[nodiscard]] virtual StoreResult assign_summary_parent(
      const std::vector<std::string>& summary_ids,
            const std::string& parent_summary_id) {
        (void)summary_ids;
        (void)parent_summary_id;
        return StoreResult::success();
    }
};

}  // namespace dasall::memory
#pragma once

#include "writeback/SummaryGenerationRequest.h"
#include "writeback/SummaryGenerationResult.h"

namespace dasall::memory {

class ISummarizer {
 public:
  virtual ~ISummarizer() = default;

  [[nodiscard]] virtual SummaryGenerationResult summarize(
      const SummaryGenerationRequest& request) = 0;
};

}  // namespace dasall::memory
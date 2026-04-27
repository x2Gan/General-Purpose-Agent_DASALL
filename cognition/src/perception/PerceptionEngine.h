#pragma once

#include <optional>

#include "CognitionConfig.h"
#include "CognitionTypes.h"
#include "perception/PerceptionResult.h"

namespace dasall::cognition::perception {

class PerceptionEngine {
 public:
  explicit PerceptionEngine(CognitionConfig config);

  [[nodiscard]] std::optional<PerceptionResult> perceive(
      const CognitionStepRequest& request) const;

 private:
  CognitionConfig config_;
};

}  // namespace dasall::cognition::perception
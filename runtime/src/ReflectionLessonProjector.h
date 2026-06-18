#pragma once

#include <optional>
#include <string>

#include "CognitionTypes.h"
#include "agent/AgentRequest.h"
#include "observation/Observation.h"
#include "writeback/MemoryWritebackRequest.h"

namespace dasall::runtime {

class ReflectionLessonProjector final {
 public:
  [[nodiscard]] std::optional<memory::MemoryWritebackRequest> make_writeback_request(
      const contracts::AgentRequest& request,
      const std::string& goal_id,
      const contracts::Observation& latest_observation,
      const cognition::CognitionReflectionResult& reflection_result) const;
};

}  // namespace dasall::runtime

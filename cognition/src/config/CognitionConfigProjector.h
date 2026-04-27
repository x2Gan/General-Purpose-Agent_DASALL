#pragma once

#include <optional>
#include <string_view>

#include "CognitionConfig.h"
#include "CognitionTypes.h"

namespace dasall::profiles {
class RuntimePolicySnapshot;
}

namespace dasall::cognition::config {

class CognitionConfigProjector {
 public:
  [[nodiscard]] static std::optional<CognitionConfig> project_config(
      const profiles::RuntimePolicySnapshot& snapshot);

  [[nodiscard]] static std::optional<StageModelHint> derive_stage_model_hint(
      const profiles::RuntimePolicySnapshot& snapshot,
      std::string_view stage_name,
      std::string_view task_type);
};

}  // namespace dasall::cognition::config
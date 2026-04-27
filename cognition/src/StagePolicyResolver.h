#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CognitionTypes.h"

namespace dasall::profiles {
class RuntimePolicySnapshot;
}

namespace dasall::cognition::policy {

enum class StageFallbackMode : std::uint8_t {
  None = 0,
  Conservative = 1,
  TemplateAllowed = 2,
  TemplatePreferred = 3,
};

struct StageExecutionPlan {
  std::vector<std::string> enabled_stages;
  std::vector<StageModelHint> stage_model_hints;
  ModelCapabilityTier preferred_model_tier = ModelCapabilityTier::Standard;
  StageFallbackMode fallback_mode = StageFallbackMode::None;
  std::uint32_t max_plan_nodes = 0;
  std::uint32_t max_plan_depth = 0;
  std::uint32_t deadline_ms = 0;
  std::uint32_t reflection_round_limit = 2;
  float clarification_threshold = 0.0F;
  bool degraded_mode_active = false;
  bool rule_fallback_enabled = false;
  bool template_fallback_enabled = false;
};

class StagePolicyResolver {
 public:
  [[nodiscard]] static std::optional<StageExecutionPlan> resolve_decide_plan(
      const profiles::RuntimePolicySnapshot& snapshot,
      const CognitionStepRequest& request);

  [[nodiscard]] static std::optional<StageExecutionPlan> resolve_reflection_plan(
      const profiles::RuntimePolicySnapshot& snapshot,
      const ReflectionRequest& request);

  [[nodiscard]] static std::optional<StageExecutionPlan> resolve_response_plan(
      const profiles::RuntimePolicySnapshot& snapshot,
      const ResponseBuildRequest& request);

  [[nodiscard]] static std::optional<StageModelHint> derive_stage_model_hint(
      const profiles::RuntimePolicySnapshot& snapshot,
      std::string_view stage_name,
      std::string_view task_type);
};

}  // namespace dasall::cognition::policy
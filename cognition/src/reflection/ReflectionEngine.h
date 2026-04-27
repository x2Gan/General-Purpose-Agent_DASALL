#pragma once

#include <string>
#include <vector>

#include "CognitionConfig.h"
#include "IReflectionEngine.h"

namespace dasall::cognition::reflection {

class ReflectionEngine final : public IReflectionEngine {
 public:
  explicit ReflectionEngine(CognitionConfig config);

  [[nodiscard]] contracts::ReflectionDecision analyze(
      const ReflectionAnalysisRequest& request) override;

 private:
  struct FailureAssessment {
    std::string failure_source;
    bool retryable = false;
    bool safe_to_replan = false;
    float recoverability_score = 0.0F;
    float safety_risk = 0.0F;
  };

  struct GoalGapAssessment {
    bool goal_gap = false;
    bool local_step_failure = false;
  };

  [[nodiscard]] FailureAssessment classify_failure_source(
      const ReflectionAnalysisRequest& request) const;

  [[nodiscard]] GoalGapAssessment evaluate_goal_gap(
      const ReflectionAnalysisRequest& request,
      const FailureAssessment& failure_assessment,
      const std::vector<std::string>& belief_invalidations) const;

  [[nodiscard]] std::vector<std::string> detect_assumption_invalidations(
      const ReflectionAnalysisRequest& request) const;

  [[nodiscard]] contracts::ReflectionDecision project_reflection_decision(
      const ReflectionAnalysisRequest& request,
      const FailureAssessment& failure_assessment,
      const GoalGapAssessment& goal_gap_assessment,
      const std::vector<std::string>& belief_invalidations) const;

  [[nodiscard]] contracts::ReflectionDecision validate_reflection_contract(
      const ReflectionAnalysisRequest& request,
      contracts::ReflectionDecision decision) const;

  CognitionConfig config_;
};

}  // namespace dasall::cognition::reflection
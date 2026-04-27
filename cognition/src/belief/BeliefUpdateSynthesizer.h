#pragma once

#include <optional>
#include <vector>

#include "agent/BeliefState.h"
#include "belief/BeliefUpdateHint.h"
#include "checkpoint/ReflectionDecision.h"
#include "decision/ActionDecision.h"
#include "observation/Observation.h"
#include "perception/PerceptionResult.h"

namespace dasall::cognition::belief {

class BeliefUpdateSynthesizer {
 public:
  [[nodiscard]] BeliefUpdateHint synthesize_from_decide(
      const perception::PerceptionResult& perception_result,
      const decision::ActionDecision& action_decision,
      const std::optional<contracts::Observation>& latest_observation) const;

  [[nodiscard]] BeliefUpdateHint synthesize_from_reflection(
      const contracts::ReflectionDecision& reflection_decision,
      const contracts::BeliefState& current_belief_state,
      const std::optional<contracts::Observation>& latest_observation) const;

  [[nodiscard]] BeliefUpdateHint merge_deltas(
      const std::vector<BeliefUpdateHint>& hints) const;

  void normalize_evidence_refs(BeliefUpdateHint& hint) const;

 private:
  void drop_unverified_delta(BeliefUpdateHint& hint) const;
};

}  // namespace dasall::cognition::belief
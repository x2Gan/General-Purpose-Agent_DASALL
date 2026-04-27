#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::cognition::belief {

// Schema baseline: cognition.belief.v1.

enum class BeliefDeltaKind : std::uint8_t {
  Upsert = 0,
  Retract = 1,
};

enum class BeliefMergeMode : std::uint8_t {
  Append = 0,
  Merge = 1,
  Replace = 2,
};

struct FactDelta {
  std::string fact;
  BeliefDeltaKind delta_kind = BeliefDeltaKind::Upsert;
};

struct HypothesisDelta {
  std::string hypothesis;
  BeliefDeltaKind delta_kind = BeliefDeltaKind::Upsert;
};

struct AssumptionDelta {
  std::string assumption;
  BeliefDeltaKind delta_kind = BeliefDeltaKind::Upsert;
};

struct EvidenceRefDelta {
  std::string evidence_ref;
  BeliefDeltaKind delta_kind = BeliefDeltaKind::Upsert;
};

struct BeliefUpdateHint {
  std::vector<FactDelta> confirmed_facts_delta;
  std::vector<HypothesisDelta> hypotheses_delta;
  std::vector<AssumptionDelta> assumptions_delta;
  std::vector<EvidenceRefDelta> evidence_refs_delta;
  std::vector<std::string> missing_evidence_refs;
  std::optional<float> confidence_hint;
  BeliefMergeMode merge_mode = BeliefMergeMode::Append;
};

}  // namespace dasall::cognition::belief
#include "belief/BeliefUpdateSynthesizer.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using dasall::cognition::belief::AssumptionDelta;
using dasall::cognition::belief::BeliefDeltaKind;
using dasall::cognition::belief::BeliefMergeMode;
using dasall::cognition::belief::BeliefUpdateHint;
using dasall::cognition::belief::EvidenceRefDelta;
using dasall::cognition::belief::FactDelta;
using dasall::cognition::belief::HypothesisDelta;
using dasall::contracts::ReflectionDecisionKind;

[[nodiscard]] std::string to_lower_copy(std::string_view value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (const char character : value) {
    lowered.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(character))));
  }
  return lowered;
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
  std::size_t start = 0U;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  std::size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }

  return std::string(value.substr(start, end - start));
}

[[nodiscard]] float clamp01(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] std::vector<std::string> split_semicolon_list(
    std::string_view value) {
  std::vector<std::string> parts;
  std::size_t start = 0U;
  while (start <= value.size()) {
    const auto delimiter = value.find(';', start);
    const auto end = delimiter == std::string_view::npos ? value.size() : delimiter;
    const auto token = trim_copy(value.substr(start, end - start));
    if (!token.empty()) {
      parts.push_back(token);
    }
    if (delimiter == std::string_view::npos) {
      break;
    }
    start = delimiter + 1U;
  }
  return parts;
}

[[nodiscard]] std::vector<std::string> parse_invalidated_assumptions(
    const std::optional<std::string>& rationale) {
  if (!rationale.has_value() || rationale->empty()) {
    return {};
  }

  const auto lowered = to_lower_copy(*rationale);
  constexpr std::string_view kMarker = "invalidated assumptions:";
  const auto marker_pos = lowered.find(kMarker);
  if (marker_pos == std::string::npos) {
    return {};
  }

  const auto suffix = rationale->substr(marker_pos + kMarker.size());
  const auto failure_pos = suffix.find("; failure_source=");
  if (failure_pos == std::string::npos) {
    return split_semicolon_list(suffix);
  }

  return split_semicolon_list(std::string_view(suffix).substr(0U, failure_pos));
}

template <typename DeltaType>
void append_unique_delta(std::vector<DeltaType>& deltas, DeltaType delta) {
  const auto duplicate = std::find_if(
      deltas.begin(), deltas.end(), [&](const DeltaType& existing) {
        if constexpr (std::is_same_v<DeltaType, FactDelta>) {
          return existing.fact == delta.fact &&
                 existing.delta_kind == delta.delta_kind;
        } else if constexpr (std::is_same_v<DeltaType, HypothesisDelta>) {
          return existing.hypothesis == delta.hypothesis &&
                 existing.delta_kind == delta.delta_kind;
        } else if constexpr (std::is_same_v<DeltaType, AssumptionDelta>) {
          return existing.assumption == delta.assumption &&
                 existing.delta_kind == delta.delta_kind;
        } else {
          return existing.evidence_ref == delta.evidence_ref &&
                 existing.delta_kind == delta.delta_kind;
        }
      });

  if (duplicate == deltas.end()) {
    deltas.push_back(std::move(delta));
  }
}

[[nodiscard]] BeliefMergeMode choose_stronger_merge_mode(
    BeliefMergeMode left,
    BeliefMergeMode right) {
  auto rank = [](BeliefMergeMode mode) {
    switch (mode) {
      case BeliefMergeMode::Append:
        return 0;
      case BeliefMergeMode::Merge:
        return 1;
      case BeliefMergeMode::Replace:
        return 2;
    }
    return 0;
  };

  return rank(right) > rank(left) ? right : left;
}

}  // namespace

namespace dasall::cognition::belief {

BeliefUpdateHint BeliefUpdateSynthesizer::synthesize_from_decide(
    const perception::PerceptionResult& perception_result,
    const decision::ActionDecision& action_decision,
    const std::optional<contracts::Observation>& latest_observation) const {
  BeliefUpdateHint hint;

  for (const auto& entity : perception_result.entities) {
    if (entity.confidence < 0.75F || entity.value.empty() ||
        entity.evidence_refs.empty()) {
      continue;
    }

    append_unique_delta(
        hint.confirmed_facts_delta,
        FactDelta{.fact = entity.name + "=" + entity.value,
                  .delta_kind = BeliefDeltaKind::Upsert});
    for (const auto& evidence_ref : entity.evidence_refs) {
      append_unique_delta(
          hint.evidence_refs_delta,
          EvidenceRefDelta{.evidence_ref = evidence_ref,
                           .delta_kind = BeliefDeltaKind::Upsert});
    }
  }

  if (action_decision.tool_intent_hint.has_value() &&
      !action_decision.tool_intent_hint->tool_name.empty()) {
    append_unique_delta(
        hint.hypotheses_delta,
        HypothesisDelta{.hypothesis =
                            std::string("pending_tool:") +
                            action_decision.tool_intent_hint->tool_name,
                        .delta_kind = BeliefDeltaKind::Upsert});
    for (const auto& evidence_ref : action_decision.tool_intent_hint->evidence_refs) {
      append_unique_delta(
          hint.evidence_refs_delta,
          EvidenceRefDelta{.evidence_ref = evidence_ref,
                           .delta_kind = BeliefDeltaKind::Upsert});
    }
  }

  if (action_decision.selected_node_id.has_value() &&
      !action_decision.selected_node_id->empty()) {
    append_unique_delta(
        hint.hypotheses_delta,
        HypothesisDelta{.hypothesis =
                            std::string("selected_node:") +
                            *action_decision.selected_node_id,
                        .delta_kind = BeliefDeltaKind::Upsert});
  }

  if (latest_observation.has_value()) {
    if (latest_observation->observation_id.has_value() &&
        !latest_observation->observation_id->empty()) {
      append_unique_delta(
          hint.evidence_refs_delta,
          EvidenceRefDelta{.evidence_ref = *latest_observation->observation_id,
                           .delta_kind = BeliefDeltaKind::Upsert});
    }

    if (latest_observation->success.value_or(false) &&
        latest_observation->payload.has_value() &&
        !latest_observation->payload->empty()) {
      append_unique_delta(
          hint.confirmed_facts_delta,
          FactDelta{.fact = std::string("observation:") +
                              *latest_observation->payload,
                    .delta_kind = BeliefDeltaKind::Upsert});
    }
  }

  for (const auto& ambiguity : perception_result.ambiguities) {
    for (const auto& missing_ref : ambiguity.missing_evidence_refs) {
      if (!missing_ref.empty()) {
        hint.missing_evidence_refs.push_back(missing_ref);
      }
    }
  }

  const auto base_confidence =
      std::max(perception_result.confidence, action_decision.confidence);
  hint.confidence_hint = latest_observation.has_value() &&
                                 latest_observation->success.value_or(false)
                             ? clamp01(base_confidence + 0.05F)
                             : clamp01(base_confidence);

  hint.merge_mode =
      (perception_result.requires_clarification ||
       action_decision.decision_kind == decision::ActionDecisionKind::AskClarification)
          ? BeliefMergeMode::Merge
          : BeliefMergeMode::Append;

  normalize_evidence_refs(hint);
  drop_unverified_delta(hint);
  return hint;
}

BeliefUpdateHint BeliefUpdateSynthesizer::synthesize_from_reflection(
    const contracts::ReflectionDecision& reflection_decision,
    const contracts::BeliefState& current_belief_state,
    const std::optional<contracts::Observation>& latest_observation) const {
  BeliefUpdateHint hint;

  if (reflection_decision.relevant_observation_refs.has_value()) {
    for (const auto& evidence_ref : *reflection_decision.relevant_observation_refs) {
      append_unique_delta(
          hint.evidence_refs_delta,
          EvidenceRefDelta{.evidence_ref = evidence_ref,
                           .delta_kind = BeliefDeltaKind::Upsert});
    }
  }

  if (latest_observation.has_value() &&
      latest_observation->observation_id.has_value() &&
      !latest_observation->observation_id->empty()) {
    append_unique_delta(
        hint.evidence_refs_delta,
        EvidenceRefDelta{.evidence_ref = *latest_observation->observation_id,
                         .delta_kind = BeliefDeltaKind::Upsert});
  }

  const auto invalidated_assumptions =
      parse_invalidated_assumptions(reflection_decision.rationale);
  const auto& known_assumptions = current_belief_state.assumptions;
  for (const auto& invalidated_assumption : invalidated_assumptions) {
    if (known_assumptions.has_value() &&
        std::find(known_assumptions->begin(), known_assumptions->end(),
                  invalidated_assumption) == known_assumptions->end()) {
      continue;
    }

    append_unique_delta(
        hint.assumptions_delta,
        AssumptionDelta{.assumption = invalidated_assumption,
                        .delta_kind = BeliefDeltaKind::Retract});
  }

  if (reflection_decision.decision_kind == ReflectionDecisionKind::Replan) {
    append_unique_delta(
        hint.hypotheses_delta,
        HypothesisDelta{.hypothesis = "reflection:replan",
                        .delta_kind = BeliefDeltaKind::Upsert});
    hint.merge_mode = BeliefMergeMode::Replace;
  } else if (reflection_decision.decision_kind == ReflectionDecisionKind::RetryStep) {
    append_unique_delta(
        hint.hypotheses_delta,
        HypothesisDelta{.hypothesis = "reflection:retry_step",
                        .delta_kind = BeliefDeltaKind::Upsert});
    hint.merge_mode = BeliefMergeMode::Merge;
  } else if (reflection_decision.decision_kind == ReflectionDecisionKind::AbortSafe) {
    append_unique_delta(
        hint.hypotheses_delta,
        HypothesisDelta{.hypothesis = "reflection:abort_safe",
                        .delta_kind = BeliefDeltaKind::Upsert});
    hint.merge_mode = BeliefMergeMode::Merge;
  } else {
    hint.merge_mode = BeliefMergeMode::Append;
  }

  if (reflection_decision.confidence.has_value()) {
    hint.confidence_hint = clamp01(*reflection_decision.confidence);
  }

  normalize_evidence_refs(hint);
  drop_unverified_delta(hint);
  return hint;
}

BeliefUpdateHint BeliefUpdateSynthesizer::merge_deltas(
    const std::vector<BeliefUpdateHint>& hints) const {
  BeliefUpdateHint merged;
  for (const auto& hint : hints) {
    merged.merge_mode =
        choose_stronger_merge_mode(merged.merge_mode, hint.merge_mode);

    if (hint.confidence_hint.has_value()) {
      merged.confidence_hint = merged.confidence_hint.has_value()
                                   ? std::max(*merged.confidence_hint,
                                              *hint.confidence_hint)
                                   : hint.confidence_hint;
    }

    merged.confirmed_facts_delta.insert(
        merged.confirmed_facts_delta.end(),
        hint.confirmed_facts_delta.begin(),
        hint.confirmed_facts_delta.end());
    merged.hypotheses_delta.insert(
        merged.hypotheses_delta.end(),
        hint.hypotheses_delta.begin(),
        hint.hypotheses_delta.end());
    merged.assumptions_delta.insert(
        merged.assumptions_delta.end(),
        hint.assumptions_delta.begin(),
        hint.assumptions_delta.end());
    merged.evidence_refs_delta.insert(
        merged.evidence_refs_delta.end(),
        hint.evidence_refs_delta.begin(),
        hint.evidence_refs_delta.end());
    merged.missing_evidence_refs.insert(
        merged.missing_evidence_refs.end(),
        hint.missing_evidence_refs.begin(),
        hint.missing_evidence_refs.end());
  }

  normalize_evidence_refs(merged);
  drop_unverified_delta(merged);
  return merged;
}

void BeliefUpdateSynthesizer::normalize_evidence_refs(
    BeliefUpdateHint& hint) const {
  std::vector<EvidenceRefDelta> normalized_refs;
  for (const auto& delta : hint.evidence_refs_delta) {
    const auto evidence_ref = trim_copy(delta.evidence_ref);
    if (evidence_ref.empty()) {
      continue;
    }

    const auto existing = std::find_if(
        normalized_refs.begin(), normalized_refs.end(),
        [&](const EvidenceRefDelta& probe) { return probe.evidence_ref == evidence_ref; });
    if (existing == normalized_refs.end()) {
      normalized_refs.push_back(
          EvidenceRefDelta{.evidence_ref = evidence_ref,
                           .delta_kind = delta.delta_kind});
      continue;
    }

    if (delta.delta_kind == BeliefDeltaKind::Retract) {
      existing->delta_kind = BeliefDeltaKind::Retract;
    }
  }
  hint.evidence_refs_delta = std::move(normalized_refs);

  std::vector<std::string> normalized_missing_refs;
  for (const auto& missing_ref : hint.missing_evidence_refs) {
    const auto trimmed = trim_copy(missing_ref);
    if (trimmed.empty()) {
      continue;
    }

    const auto present_as_evidence = std::find_if(
        hint.evidence_refs_delta.begin(), hint.evidence_refs_delta.end(),
        [&](const EvidenceRefDelta& delta) { return delta.evidence_ref == trimmed; });
    if (present_as_evidence != hint.evidence_refs_delta.end()) {
      continue;
    }

    if (std::find(normalized_missing_refs.begin(), normalized_missing_refs.end(), trimmed) ==
        normalized_missing_refs.end()) {
      normalized_missing_refs.push_back(trimmed);
    }
  }
  hint.missing_evidence_refs = std::move(normalized_missing_refs);
}

void BeliefUpdateSynthesizer::drop_unverified_delta(
    BeliefUpdateHint& hint) const {
  if (!hint.evidence_refs_delta.empty()) {
    return;
  }

  hint.confirmed_facts_delta.clear();
  hint.hypotheses_delta.clear();
  hint.assumptions_delta.clear();
}

}  // namespace dasall::cognition::belief
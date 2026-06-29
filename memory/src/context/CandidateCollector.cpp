#include "context/CandidateCollector.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "util/TokenEstimator.h"

namespace dasall::memory {
namespace {

void append_warning(std::vector<std::string>& warnings,
                    const std::string& warning) {
  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(warning);
  }
}

void add_optional_string_tokens(const util::ITokenEstimator& token_estimator,
                                const std::optional<std::string>& value,
                                int& total) {
  if (value.has_value()) {
    total += token_estimator.estimate_text_tokens(*value);
  }
}

void add_string_vector_tokens(const util::ITokenEstimator& token_estimator,
                              const std::vector<std::string>& values,
                              int& total) {
  for (const auto& value : values) {
    total += token_estimator.estimate_text_tokens(value);
  }
}

void add_optional_string_vector_tokens(
    const util::ITokenEstimator& token_estimator,
    const std::optional<std::vector<std::string>>& values,
    int& total) {
  if (values.has_value()) {
    add_string_vector_tokens(token_estimator, *values, total);
  }
}

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

template <typename MetadataMap>
[[nodiscard]] double bounded_score_or_default(
    const MetadataMap& metadata,
    const std::optional<std::string>& id,
    double default_value) {
  if (!id.has_value() || id->empty()) {
    return default_value;
  }

  const auto it = metadata.find(*id);
  if (it == metadata.end()) {
    return default_value;
  }

  return std::clamp(it->second, 0.0, 1.0);
}

[[nodiscard]] double normalize_percent_score(
    const std::optional<std::uint32_t>& score) {
  if (!score.has_value()) {
    return 0.0;
  }

  return std::clamp(static_cast<double>(*score) / 100.0, 0.0, 1.0);
}

[[nodiscard]] double fact_source_weight(const contracts::MemoryFact& fact) {
  double weight = 0.10;
  if (fact.source_turn_ids.has_value() && !fact.source_turn_ids->empty()) {
    weight += 0.30;
  }
  if (fact.source_observation_refs.has_value() &&
      !fact.source_observation_refs->empty()) {
    weight += 0.35;
  }
  if (fact.evidence_digest.has_value() && !fact.evidence_digest->empty()) {
    weight += 0.15;
  }
  if (fact.fact_type.has_value() && !fact.fact_type->empty()) {
    weight += 0.05;
  }
  if (fact.tags.has_value() && !fact.tags->empty()) {
    weight += 0.05;
  }
  return std::clamp(weight, 0.0, 1.0);
}

[[nodiscard]] double experience_source_weight(
    const contracts::ExperienceMemory& experience) {
  double weight = 0.10;
  if (experience.source_fact_ids.has_value() &&
      !experience.source_fact_ids->empty()) {
    weight += 0.35;
  }
  if (experience.source_turn_ids.has_value() &&
      !experience.source_turn_ids->empty()) {
    weight += 0.25;
  }
  if (experience.applicable_domains.has_value() &&
      !experience.applicable_domains->empty()) {
    weight += 0.15;
  }
  if (experience.risk_notes.has_value() && !experience.risk_notes->empty()) {
    weight += 0.10;
  }
  if (experience.tags.has_value() && !experience.tags->empty()) {
    weight += 0.05;
  }
  return std::clamp(weight, 0.0, 1.0);
}

[[nodiscard]] double composite_score_or_fallback(
    const ContextConfig::ScoringConfig& scoring,
    double confidence,
    double recency,
    double hit_rate,
    double source_weight) {
  if (!scoring.composite_enabled) {
    return confidence;
  }

  const auto confidence_weight = std::max(0.0, scoring.confidence_weight);
  const auto recency_weight = std::max(0.0, scoring.recency_weight);
  const auto hit_rate_weight = std::max(0.0, scoring.hit_rate_weight);
  const auto provenance_weight = std::max(0.0, scoring.source_weight);
  const auto total_weight =
      confidence_weight + recency_weight + hit_rate_weight + provenance_weight;
  if (total_weight <= 0.0) {
    return confidence;
  }

  return ((confidence_weight * confidence) +
          (recency_weight * recency) +
          (hit_rate_weight * hit_rate) +
          (provenance_weight * source_weight)) /
         total_weight;
}

}  // namespace

CandidateCollector::CandidateCollector(IWorkingMemoryBoard& working_memory_board,
                                       IMemoryStore& store,
                                       const MemoryConfig& config,
                                       VectorMemoryIndexAdapter* vector_index,
                                       std::shared_ptr<const util::ITokenEstimator> token_estimator,
                                       std::shared_ptr<std::mutex> writer_mutex)
    : working_memory_board_(working_memory_board),
      store_(store),
      context_config_(config.context),
      vector_config_(config.vector),
      vector_index_(vector_index),
      token_estimator_(token_estimator != nullptr ? token_estimator
                                                  : util::create_token_estimator(config)),
      writer_mutex_(std::move(writer_mutex)) {}

CandidateSet CandidateCollector::collect(const CandidateCollectRequest& request) {
  CandidateSet set;
  set.external_evidence = request.external_evidence;
  set.working_snapshot.session_id = request.session_id;

  try {
    set.working_snapshot = working_memory_board_.export_snapshot(request.session_id);
  } catch (...) {
    append_warning(set.warnings, "working_memory_snapshot_unavailable");
  }

  try {
    set.session_bundle = load_session_context(request.session_id);
  } catch (...) {
    set.session_bundle.session.session_id = request.session_id;
    append_warning(set.warnings, "session_bundle_unavailable");
  }

  try {
    set.latest_summary = store_.load_latest_summary(request.session_id);
  } catch (...) {
    append_warning(set.warnings, "summary_query_unavailable");
  }

  try {
    set.relevant_facts = query_relevant_facts(request, set.session_bundle);
    touch_fact_access(set.relevant_facts, set.warnings);
  } catch (...) {
    append_warning(set.warnings, "fact_query_unavailable");
  }

  try {
    set.relevant_experiences =
        query_relevant_experiences(request, set.session_bundle);
    touch_experience_access(set.relevant_experiences, set.warnings);
  } catch (...) {
    append_warning(set.warnings, "experience_query_unavailable");
  }

  if (vector_config_.enabled && !request.goal_summary.empty() &&
      (vector_index_ == nullptr || !vector_index_->is_available())) {
    append_warning(set.warnings, "vector_unavailable");
  }

  try {
    set.vector_hits = search_vector(request);
  } catch (...) {
    append_warning(set.warnings, "vector_query_unavailable");
  }

  set.estimated_total_tokens = estimate_tokens(set);
  return set;
}

SessionLoadBundle CandidateCollector::load_session_context(
    const std::string& session_id) const {
  return store_.load_session_bundle(SessionLoadRequest{
      .session_id = session_id,
      .recent_turn_limit = std::max(1, context_config_.recent_turn_limit),
  });
}

std::vector<contracts::MemoryFact> CandidateCollector::query_relevant_facts(
    const CandidateCollectRequest& request,
    const SessionLoadBundle& session_bundle) const {
  FactQuery query;
  FactQueryResult result;
  if (session_bundle.session.user_id.has_value() &&
      !session_bundle.session.user_id->empty()) {
    query.min_confidence = std::max(0, context_config_.fact_confidence_floor);
    query.exclude_superseded = true;
    query.limit = 50;
    result = store_.query_facts_by_user(*session_bundle.session.user_id, query);
  } else {
    if (!request.session_id.empty()) {
      query.session_id = request.session_id;
    }

    query.min_confidence = std::max(0, context_config_.fact_confidence_floor);
    query.exclude_superseded = true;
    query.limit = 50;
    result = store_.query_facts(query);
  }

  auto facts = std::move(result.facts);
  std::stable_sort(facts.begin(), facts.end(), [this, &result](
                                                           const contracts::MemoryFact& left,
                                                           const contracts::MemoryFact& right) {
    const auto left_score = composite_score_or_fallback(
        context_config_.scoring,
        normalize_percent_score(left.confidence_score),
        bounded_score_or_default(result.recency_score_by_fact_id, left.fact_id, 1.0),
        bounded_score_or_default(result.hit_rate_score_by_fact_id, left.fact_id, 0.0),
        fact_source_weight(left));
    const auto right_score = composite_score_or_fallback(
        context_config_.scoring,
        normalize_percent_score(right.confidence_score),
        bounded_score_or_default(result.recency_score_by_fact_id, right.fact_id, 1.0),
        bounded_score_or_default(result.hit_rate_score_by_fact_id, right.fact_id, 0.0),
        fact_source_weight(right));
    if (left_score != right_score) {
      return left_score > right_score;
    }
    if (left.confidence_score != right.confidence_score) {
      return left.confidence_score.value_or(0U) > right.confidence_score.value_or(0U);
    }
    return left.fact_id.value_or(std::string{}) < right.fact_id.value_or(std::string{});
  });
  return facts;
}

std::vector<contracts::ExperienceMemory>
CandidateCollector::query_relevant_experiences(
    const CandidateCollectRequest& request,
    const SessionLoadBundle& session_bundle) const {
  ExperienceQuery query;
  if (!request.session_id.empty()) {
    query.session_id = request.session_id;
  }

  if (session_bundle.session.user_id.has_value() &&
      !session_bundle.session.user_id->empty()) {
    query.user_id = session_bundle.session.user_id;
  }

  if (!request.stage.empty()) {
    query.stage = request.stage;
  }

  query.exclude_expired = true;
  query.limit = 20;
  auto result = store_.query_experiences(query);
  auto experiences = std::move(result.experiences);
  std::stable_sort(experiences.begin(), experiences.end(), [this, &result](
                       const contracts::ExperienceMemory& left,
                       const contracts::ExperienceMemory& right) {
    const auto left_score = composite_score_or_fallback(
      context_config_.scoring,
      normalize_percent_score(left.effectiveness_score),
      bounded_score_or_default(
        result.recency_score_by_experience_id, left.experience_id, 1.0),
      bounded_score_or_default(
        result.hit_rate_score_by_experience_id, left.experience_id, 0.0),
      experience_source_weight(left));
    const auto right_score = composite_score_or_fallback(
      context_config_.scoring,
      normalize_percent_score(right.effectiveness_score),
      bounded_score_or_default(
        result.recency_score_by_experience_id, right.experience_id, 1.0),
      bounded_score_or_default(
        result.hit_rate_score_by_experience_id, right.experience_id, 0.0),
      experience_source_weight(right));
    if (left_score != right_score) {
      return left_score > right_score;
    }
    if (left.effectiveness_score != right.effectiveness_score) {
      return left.effectiveness_score.value_or(0U) >
             right.effectiveness_score.value_or(0U);
    }
    return left.experience_id.value_or(std::string{}) <
           right.experience_id.value_or(std::string{});
  });
  return experiences;
}

void CandidateCollector::touch_fact_access(
    const std::vector<contracts::MemoryFact>& facts,
    std::vector<std::string>& warnings) const {
  std::vector<std::string> fact_ids;
  fact_ids.reserve(facts.size());
  for (const auto& fact : facts) {
    if (fact.fact_id.has_value() && !fact.fact_id->empty()) {
      fact_ids.push_back(*fact.fact_id);
    }
  }

  if (fact_ids.empty()) {
    return;
  }

  std::unique_lock<std::mutex> writer_lock;
  if (writer_mutex_) {
    writer_lock = std::unique_lock<std::mutex>(*writer_mutex_);
  }

  const auto touch_result = store_.touch_facts(fact_ids, current_time_millis());
  if (!touch_result.ok) {
    append_warning(warnings, "fact_touch_unavailable");
  }
}

void CandidateCollector::touch_experience_access(
    const std::vector<contracts::ExperienceMemory>& experiences,
    std::vector<std::string>& warnings) const {
  std::vector<std::string> experience_ids;
  experience_ids.reserve(experiences.size());
  for (const auto& experience : experiences) {
    if (experience.experience_id.has_value() && !experience.experience_id->empty()) {
      experience_ids.push_back(*experience.experience_id);
    }
  }

  if (experience_ids.empty()) {
    return;
  }

  std::unique_lock<std::mutex> writer_lock;
  if (writer_mutex_) {
    writer_lock = std::unique_lock<std::mutex>(*writer_mutex_);
  }

  const auto touch_result =
      store_.touch_experiences(experience_ids, current_time_millis());
  if (!touch_result.ok) {
    append_warning(warnings, "experience_touch_unavailable");
  }
}

std::vector<VectorHit> CandidateCollector::search_vector(
    const CandidateCollectRequest& request) const {
  if (!vector_config_.enabled || request.goal_summary.empty() ||
      vector_index_ == nullptr || !vector_index_->is_available()) {
    return {};
  }

  return vector_index_->search(
      request.goal_summary,
      std::max(1, vector_config_.search_top_k));
}

int CandidateCollector::estimate_tokens(const CandidateSet& set) const {
  const auto& token_estimator = *token_estimator_;
  int total = 0;

  for (const auto& slot : set.working_snapshot.slots) {
    total += token_estimator.estimate_text_tokens(slot.key);
    total += token_estimator.estimate_text_tokens(slot.value);
    total += token_estimator.estimate_text_tokens(slot.source);
  }
  add_string_vector_tokens(token_estimator, set.working_snapshot.open_questions, total);
  add_string_vector_tokens(token_estimator, set.working_snapshot.ephemeral_facts, total);

  add_optional_string_tokens(token_estimator, set.session_bundle.session.session_id, total);
  add_optional_string_tokens(token_estimator, set.session_bundle.session.user_id, total);
  add_optional_string_tokens(token_estimator, set.session_bundle.session.metadata_digest, total);
  add_optional_string_tokens(
      token_estimator, set.session_bundle.session.latest_summary_memory_ref, total);
  add_optional_string_vector_tokens(token_estimator, set.session_bundle.session.turn_ids, total);
  add_optional_string_vector_tokens(token_estimator, set.session_bundle.session.tags, total);

  for (const auto& turn : set.session_bundle.recent_turns) {
    add_optional_string_tokens(token_estimator, turn.turn_id, total);
    add_optional_string_tokens(token_estimator, turn.user_input, total);
    add_optional_string_tokens(token_estimator, turn.agent_response, total);
    add_optional_string_tokens(token_estimator, turn.summary_memory_ref, total);
    add_optional_string_vector_tokens(token_estimator, turn.tool_call_refs, total);
    add_optional_string_vector_tokens(token_estimator, turn.observation_refs, total);
    add_optional_string_vector_tokens(token_estimator, turn.tags, total);
  }

  if (set.latest_summary.has_value()) {
    add_optional_string_tokens(token_estimator, set.latest_summary->summary_text, total);
    add_optional_string_vector_tokens(
        token_estimator, set.latest_summary->source_turn_ids, total);
    add_optional_string_vector_tokens(
        token_estimator, set.latest_summary->decisions_made, total);
    add_optional_string_vector_tokens(
        token_estimator, set.latest_summary->confirmed_facts, total);
    add_optional_string_vector_tokens(
        token_estimator, set.latest_summary->tool_outcomes, total);
    add_optional_string_vector_tokens(token_estimator, set.latest_summary->tags, total);
  }

  for (const auto& fact : set.relevant_facts) {
    add_optional_string_tokens(token_estimator, fact.fact_id, total);
    add_optional_string_tokens(token_estimator, fact.fact_text, total);
    add_optional_string_tokens(token_estimator, fact.fact_type, total);
    add_optional_string_tokens(token_estimator, fact.evidence_digest, total);
    add_optional_string_vector_tokens(token_estimator, fact.source_turn_ids, total);
    add_optional_string_vector_tokens(
        token_estimator, fact.source_observation_refs, total);
    add_optional_string_vector_tokens(token_estimator, fact.tags, total);
  }

  for (const auto& experience : set.relevant_experiences) {
    add_optional_string_tokens(token_estimator, experience.experience_id, total);
    add_optional_string_tokens(token_estimator, experience.lesson_summary, total);
    add_optional_string_tokens(token_estimator, experience.trigger_condition, total);
    add_optional_string_tokens(token_estimator, experience.recommended_action, total);
    add_optional_string_tokens(token_estimator, experience.risk_notes, total);
    add_optional_string_vector_tokens(token_estimator, experience.source_fact_ids, total);
    add_optional_string_vector_tokens(token_estimator, experience.source_turn_ids, total);
    add_optional_string_vector_tokens(
        token_estimator, experience.applicable_domains, total);
    add_optional_string_vector_tokens(token_estimator, experience.tags, total);
  }

  add_string_vector_tokens(token_estimator, set.external_evidence, total);

  for (const auto& hit : set.vector_hits) {
    total += token_estimator.estimate_text_tokens(hit.doc_id);
    total += token_estimator.estimate_text_tokens(hit.doc_type);
    total += token_estimator.estimate_text_tokens(hit.text_snippet);
  }

  if (total <= 0) {
    return 0;
  }

  return total + std::max(1, (total + 9) / 10);
}

}  // namespace dasall::memory
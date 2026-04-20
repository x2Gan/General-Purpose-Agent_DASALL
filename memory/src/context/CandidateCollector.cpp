#include "context/CandidateCollector.h"

#include <algorithm>
#include <string_view>

namespace dasall::memory {
namespace {

void append_warning(std::vector<std::string>& warnings,
                    const std::string& warning) {
  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(warning);
  }
}

int estimate_text_tokens(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  int ascii_bytes = 0;
  int multibyte_characters = 0;

  for (const unsigned char byte : text) {
    if (byte < 0x80U) {
      ++ascii_bytes;
      continue;
    }

    if ((byte & 0xC0U) != 0x80U) {
      ++multibyte_characters;
    }
  }

  return std::max(1, ((ascii_bytes + 3) / 4) + (multibyte_characters * 2));
}

void add_optional_string_tokens(const std::optional<std::string>& value,
                                int& total) {
  if (value.has_value()) {
    total += estimate_text_tokens(*value);
  }
}

void add_string_vector_tokens(const std::vector<std::string>& values, int& total) {
  for (const auto& value : values) {
    total += estimate_text_tokens(value);
  }
}

void add_optional_string_vector_tokens(
    const std::optional<std::vector<std::string>>& values,
    int& total) {
  if (values.has_value()) {
    add_string_vector_tokens(*values, total);
  }
}

}  // namespace

CandidateCollector::CandidateCollector(IWorkingMemoryBoard& working_memory_board,
                                       IMemoryStore& store,
                                       const MemoryConfig& config,
                                       VectorMemoryIndexAdapter* vector_index)
    : working_memory_board_(working_memory_board),
      store_(store),
      context_config_(config.context),
      vector_config_(config.vector),
      vector_index_(vector_index) {}

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
  } catch (...) {
    append_warning(set.warnings, "fact_query_unavailable");
  }

  try {
    set.relevant_experiences =
        query_relevant_experiences(request, set.session_bundle);
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
  if (!request.session_id.empty()) {
    query.session_id = request.session_id;
  }

  if (session_bundle.session.user_id.has_value() &&
      !session_bundle.session.user_id->empty()) {
    query.user_id = session_bundle.session.user_id;
  }

  query.min_confidence = std::max(0, context_config_.fact_confidence_floor);
  query.exclude_superseded = true;
  query.limit = 50;
  return store_.query_facts(query).facts;
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
  return store_.query_experiences(query).experiences;
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
  int total = 0;

  for (const auto& slot : set.working_snapshot.slots) {
    total += estimate_text_tokens(slot.key);
    total += estimate_text_tokens(slot.value);
    total += estimate_text_tokens(slot.source);
  }
  add_string_vector_tokens(set.working_snapshot.open_questions, total);
  add_string_vector_tokens(set.working_snapshot.ephemeral_facts, total);

  add_optional_string_tokens(set.session_bundle.session.session_id, total);
  add_optional_string_tokens(set.session_bundle.session.user_id, total);
  add_optional_string_tokens(set.session_bundle.session.metadata_digest, total);
  add_optional_string_tokens(set.session_bundle.session.latest_summary_memory_ref, total);
  add_optional_string_vector_tokens(set.session_bundle.session.turn_ids, total);
  add_optional_string_vector_tokens(set.session_bundle.session.tags, total);

  for (const auto& turn : set.session_bundle.recent_turns) {
    add_optional_string_tokens(turn.turn_id, total);
    add_optional_string_tokens(turn.user_input, total);
    add_optional_string_tokens(turn.agent_response, total);
    add_optional_string_tokens(turn.summary_memory_ref, total);
    add_optional_string_vector_tokens(turn.tool_call_refs, total);
    add_optional_string_vector_tokens(turn.observation_refs, total);
    add_optional_string_vector_tokens(turn.tags, total);
  }

  if (set.latest_summary.has_value()) {
    add_optional_string_tokens(set.latest_summary->summary_text, total);
    add_optional_string_vector_tokens(set.latest_summary->source_turn_ids, total);
    add_optional_string_vector_tokens(set.latest_summary->decisions_made, total);
    add_optional_string_vector_tokens(set.latest_summary->confirmed_facts, total);
    add_optional_string_vector_tokens(set.latest_summary->tool_outcomes, total);
    add_optional_string_vector_tokens(set.latest_summary->tags, total);
  }

  for (const auto& fact : set.relevant_facts) {
    add_optional_string_tokens(fact.fact_id, total);
    add_optional_string_tokens(fact.fact_text, total);
    add_optional_string_tokens(fact.fact_type, total);
    add_optional_string_tokens(fact.evidence_digest, total);
    add_optional_string_vector_tokens(fact.source_turn_ids, total);
    add_optional_string_vector_tokens(fact.source_observation_refs, total);
    add_optional_string_vector_tokens(fact.tags, total);
  }

  for (const auto& experience : set.relevant_experiences) {
    add_optional_string_tokens(experience.experience_id, total);
    add_optional_string_tokens(experience.lesson_summary, total);
    add_optional_string_tokens(experience.trigger_condition, total);
    add_optional_string_tokens(experience.recommended_action, total);
    add_optional_string_tokens(experience.risk_notes, total);
    add_optional_string_vector_tokens(experience.source_fact_ids, total);
    add_optional_string_vector_tokens(experience.source_turn_ids, total);
    add_optional_string_vector_tokens(experience.applicable_domains, total);
    add_optional_string_vector_tokens(experience.tags, total);
  }

  add_string_vector_tokens(set.external_evidence, total);

  for (const auto& hit : set.vector_hits) {
    total += estimate_text_tokens(hit.doc_id);
    total += estimate_text_tokens(hit.doc_type);
    total += estimate_text_tokens(hit.text_snippet);
  }

  if (total <= 0) {
    return 0;
  }

  return total + std::max(1, (total + 9) / 10);
}

}  // namespace dasall::memory
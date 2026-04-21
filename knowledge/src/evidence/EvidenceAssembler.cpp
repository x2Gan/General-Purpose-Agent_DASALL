#include "evidence/EvidenceAssembler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>
#include <utility>

namespace dasall::knowledge::evidence {

namespace {

void append_unique(std::vector<std::string>& values, std::string value) {
  if (value.empty()) {
    return;
  }

  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

[[nodiscard]] std::string collapse_whitespace(const std::string_view& text) {
  std::string collapsed;
  collapsed.reserve(text.size());

  bool pending_space = false;
  for (const unsigned char character : text) {
    if (std::isspace(character) != 0) {
      pending_space = !collapsed.empty();
      continue;
    }

    if (pending_space) {
      collapsed.push_back(' ');
      pending_space = false;
    }

    collapsed.push_back(static_cast<char>(character));
  }

  return collapsed;
}

[[nodiscard]] std::string_view authority_label(AuthorityLevel authority_level) {
  switch (authority_level) {
    case AuthorityLevel::Normative:
      return "normative";
    case AuthorityLevel::Reference:
      return "reference";
    case AuthorityLevel::Advisory:
      return "advisory";
  }

  return "reference";
}

[[nodiscard]] std::size_t estimate_tokens(const std::string_view& text,
                                          std::size_t chars_per_token) {
  if (text.empty() || chars_per_token == 0U) {
    return 0U;
  }

  return static_cast<std::size_t>(std::ceil(static_cast<double>(text.size()) /
                                            static_cast<double>(chars_per_token)));
}

[[nodiscard]] std::size_t safe_budget_tokens(const EvidenceAssemblePolicy& policy) {
  if (policy.evidence_budget_tokens == 0U) {
    return 0U;
  }

  const auto safe_budget = static_cast<std::size_t>(std::floor(
      static_cast<double>(policy.evidence_budget_tokens) *
      (1.0 - static_cast<double>(policy.budget_safety_margin_ratio))));
  return std::max<std::size_t>(1U, safe_budget);
}

[[nodiscard]] std::string build_evidence_id(const retrieve::RecallHit& hit) {
  return hit.corpus_id + "/" + hit.document_id + "/" + hit.chunk_id;
}

[[nodiscard]] std::string build_coverage_notes(std::size_t input_hit_count,
                                               std::size_t valid_hit_count,
                                               bool projection_truncated,
                                               bool projection_budget_exhausted,
                                               std::size_t dropped_invalid_hits,
                                               bool upstream_degraded) {
  std::vector<std::string> notes;

  if (input_hit_count == 0U) {
    notes.push_back("no_ranked_hits");
  } else if (valid_hit_count == 0U) {
    notes.push_back("no_valid_ranked_hits");
  } else if (projection_budget_exhausted) {
    notes.push_back("projection_budget_exhausted");
  } else if (projection_truncated) {
    notes.push_back("projection_truncated");
  } else {
    notes.push_back("full_coverage");
  }

  if (upstream_degraded) {
    notes.push_back("upstream_degraded");
  }
  if (dropped_invalid_hits > 0U) {
    notes.push_back("dropped_invalid_hits=" + std::to_string(dropped_invalid_hits));
  }

  std::string joined;
  for (std::size_t index = 0; index < notes.size(); ++index) {
    if (index > 0U) {
      joined.append("; ");
    }
    joined.append(notes[index]);
  }

  return joined;
}

}  // namespace

bool EvidenceAssemblePolicy::has_consistent_values() const {
  return evidence_budget_tokens > 0U && max_context_projection_items > 0U &&
         stale_confidence_penalty >= 0.0F && stale_confidence_penalty <= 1.0F &&
         chars_per_token > 0U && budget_safety_margin_ratio >= 0.0F &&
         budget_safety_margin_ratio < 1.0F;
}

EvidenceAssemblePolicy EvidenceAssemblePolicy::from_query(const KnowledgeQuery& query,
                                                          const KnowledgeConfigSnapshot& config) {
  auto max_projection_items = query.max_context_projection_items > 0U
                                  ? query.max_context_projection_items
                                  : config.max_context_projection_items;
  if (config.max_context_projection_items > 0U) {
    max_projection_items = std::min(max_projection_items, config.max_context_projection_items);
  }

  return EvidenceAssemblePolicy{
      .evidence_budget_tokens = query.retrieval_evidence_budget_hint > 0U
                                    ? query.retrieval_evidence_budget_hint
                                    : config.evidence_budget_tokens,
      .max_context_projection_items = max_projection_items,
  };
}

EvidenceBundle EvidenceAssembler::assemble(const rerank::RankedHitSet& hits,
                                           const EvidenceAssemblePolicy& policy) const {
  if (!policy.has_consistent_values()) {
    return EvidenceBundle{
        .slices = {},
        .context_projection = {},
        .omitted_sources = {},
        .degraded = true,
        .evidence_insufficient = true,
        .coverage_notes = "invalid_policy",
    };
  }

  EvidenceBundle bundle;
  const auto projection_budget_tokens = safe_budget_tokens(policy);

  std::size_t dropped_invalid_hits = 0U;
  float max_fused_score = 0.0F;
  std::vector<const rerank::RankedHit*> valid_hits;
  std::vector<std::string> seen_chunk_ids;

  for (const auto& hit : hits.hits) {
    if (!hit.has_consistent_values()) {
      ++dropped_invalid_hits;
      continue;
    }

    if (std::find(seen_chunk_ids.begin(), seen_chunk_ids.end(), hit.hit.chunk_id) !=
        seen_chunk_ids.end()) {
      ++dropped_invalid_hits;
      continue;
    }

    if (collapse_whitespace(hit.hit.raw_snippet).empty()) {
      ++dropped_invalid_hits;
      continue;
    }

    seen_chunk_ids.push_back(hit.hit.chunk_id);
    valid_hits.push_back(&hit);
    max_fused_score = std::max(max_fused_score, hit.fused_score);
  }

  std::size_t consumed_projection_tokens = 0U;
  for (const auto* hit : valid_hits) {
    const auto slice = build_slice(*hit, max_fused_score, policy.stale_confidence_penalty);
    if (!slice.has_consistent_values()) {
      ++dropped_invalid_hits;
      continue;
    }

    bundle.slices.push_back(slice);

    if (bundle.context_projection.size() >= policy.max_context_projection_items) {
      append_unique(bundle.omitted_sources, slice.citation_ref);
      continue;
    }

    const auto projection_line = build_projection_line(*hit);
    const auto projected_tokens = estimate_tokens(projection_line, policy.chars_per_token);
    if (projection_line.empty() ||
        consumed_projection_tokens + projected_tokens > projection_budget_tokens) {
      append_unique(bundle.omitted_sources, slice.citation_ref);
      continue;
    }

    bundle.context_projection.push_back(projection_line);
    consumed_projection_tokens += projected_tokens;
  }

  bundle.degraded = hits.degraded || dropped_invalid_hits > 0U || !bundle.omitted_sources.empty();
  bundle.evidence_insufficient = bundle.slices.empty() || bundle.context_projection.empty();
  bundle.coverage_notes = build_coverage_notes(hits.hits.size(),
                                               bundle.slices.size(),
                                               !bundle.omitted_sources.empty(),
                                               !bundle.slices.empty() &&
                                                   bundle.context_projection.empty(),
                                               dropped_invalid_hits,
                                               hits.degraded);
  return bundle;
}

std::string EvidenceAssembler::build_projection_line(const rerank::RankedHit& hit) const {
  const auto snippet = collapse_whitespace(hit.hit.raw_snippet);
  if (snippet.empty()) {
    return {};
  }

  std::string line = "[";
  line.append(authority_label(hit.hit.authority_level));
  line.push_back(']');
  if (hit.stale) {
    line.append("[stale]");
  }
  line.push_back(' ');
  line.append(snippet);
  line.append(" (");
  line.append(hit.hit.citation_ref);
  line.push_back(')');
  return line;
}

EvidenceSlice EvidenceAssembler::build_slice(const rerank::RankedHit& hit,
                                             float max_fused_score,
                                             float stale_confidence_penalty) const {
  const auto normalized_snippet = collapse_whitespace(hit.hit.raw_snippet);
  float confidence = max_fused_score > 0.0F ? hit.fused_score / max_fused_score : 0.0F;
  if (hit.stale) {
    confidence *= (1.0F - stale_confidence_penalty);
  }

  return EvidenceSlice{
      .evidence_id = build_evidence_id(hit.hit),
      .snippet = normalized_snippet,
      .citation_ref = hit.hit.citation_ref,
      .confidence = std::clamp(confidence, 0.0F, 1.0F),
      .freshness = hit.stale ? FreshnessState::StaleAllowed : FreshnessState::Fresh,
      .tags = hit.hit.tags,
  };
}

}  // namespace dasall::knowledge::evidence
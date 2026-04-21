#include "rerank/Reranker.h"

#include <algorithm>
#include <utility>

namespace dasall::knowledge::rerank {

namespace {

struct AggregateHit {
  retrieve::RecallHit hit;
  float raw_rrf_score = 0.0F;
  bool seen_sparse = false;
  bool seen_dense = false;
};

void append_reason_code(std::vector<std::string>& reason_codes, std::string reason_code) {
  if (std::find(reason_codes.begin(), reason_codes.end(), reason_code) == reason_codes.end()) {
    reason_codes.push_back(std::move(reason_code));
  }
}

[[nodiscard]] float clamp_score(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float authority_multiplier(const RerankPolicy& policy, AuthorityLevel authority_level) {
  switch (authority_level) {
    case AuthorityLevel::Normative:
      return policy.normative_authority_boost;
    case AuthorityLevel::Reference:
      return policy.reference_authority_boost;
    case AuthorityLevel::Advisory:
      return policy.advisory_authority_boost;
  }

  return 1.0F;
}

[[nodiscard]] std::vector<AggregateHit>::iterator find_aggregate_hit(
    std::vector<AggregateHit>& aggregates,
    std::string_view chunk_id) {
  return std::find_if(aggregates.begin(), aggregates.end(), [&](const AggregateHit& aggregate) {
    return aggregate.hit.chunk_id == chunk_id;
  });
}

void add_lane_hits(const std::vector<retrieve::RecallHit>& hits,
                   bool sparse_lane,
                   std::size_t rrf_k,
                   std::vector<AggregateHit>& aggregates) {
  for (std::size_t index = 0; index < hits.size(); ++index) {
    const auto& hit = hits[index];
    const auto contribution = 1.0F / static_cast<float>(rrf_k + index + 1U);
    auto aggregate = find_aggregate_hit(aggregates, hit.chunk_id);
    if (aggregate == aggregates.end()) {
      aggregates.push_back(AggregateHit{
          .hit = hit,
          .raw_rrf_score = contribution,
          .seen_sparse = sparse_lane,
          .seen_dense = !sparse_lane,
      });
      continue;
    }

    aggregate->raw_rrf_score += contribution;
    aggregate->seen_sparse = aggregate->seen_sparse || sparse_lane;
    aggregate->seen_dense = aggregate->seen_dense || !sparse_lane;
    if (hit.score > aggregate->hit.score) {
      aggregate->hit = hit;
    }
  }
}

[[nodiscard]] RankedHitSet fallback_lexical_first(const retrieve::RecallCandidateSet& candidates,
                                                  const FreshnessSnapshot& freshness) {
  RankedHitSet ranked_hit_set;
  ranked_hit_set.degraded = true;

  std::vector<retrieve::RecallHit> merged_hits;
  auto append_unique_hits = [&](const std::vector<retrieve::RecallHit>& hits) {
    for (const auto& hit : hits) {
      const auto already_present = std::find_if(merged_hits.begin(), merged_hits.end(), [&](const auto& existing_hit) {
        return existing_hit.chunk_id == hit.chunk_id;
      });
      if (already_present == merged_hits.end()) {
        merged_hits.push_back(hit);
      }
    }
  };

  append_unique_hits(candidates.sparse_hits);
  append_unique_hits(candidates.dense_hits);

  const bool stale = freshness.state == FreshnessState::StaleAllowed && freshness.stale_read_allowed;
  for (std::size_t index = 0; index < merged_hits.size(); ++index) {
    const float score = merged_hits.empty() ? 0.0F
                                            : clamp_score(1.0F - static_cast<float>(index) /
                                                                    static_cast<float>(merged_hits.size()));
    RankedHit ranked_hit{
        .hit = merged_hits[index],
        .fused_score = score,
        .stale = stale,
        .score_reason_codes = {"fallback_lexical_first"},
    };
    if (stale) {
      append_reason_code(ranked_hit.score_reason_codes, "stale_penalty_applied");
    }
    ranked_hit_set.hits.push_back(std::move(ranked_hit));
  }

  return ranked_hit_set;
}

}  // namespace

bool RankedHit::has_consistent_values() const {
  return hit.has_consistent_values() && fused_score >= 0.0F && fused_score <= 1.0F &&
         detail::has_unique_values(score_reason_codes);
}

bool RankedHitSet::has_consistent_values() const {
  std::vector<std::string> chunk_ids;
  chunk_ids.reserve(hits.size());
  for (const auto& hit : hits) {
    if (!hit.has_consistent_values()) {
      return false;
    }
    chunk_ids.push_back(hit.hit.chunk_id);
  }

  return detail::has_unique_values(chunk_ids);
}

bool RerankPolicy::has_consistent_values() const {
  return top_k > 0U && rrf_k >= 1U && rrf_k <= 200U && stale_penalty_factor > 0.0F &&
         stale_penalty_factor <= 1.0F && normative_authority_boost >= 1.0F &&
         reference_authority_boost > 0.0F && advisory_authority_boost > 0.0F &&
         min_score_cutoff >= 0.0F && min_score_cutoff <= 1.0F;
}

RankedHitSet Reranker::rerank(const retrieve::RecallCandidateSet& candidates,
                              const FreshnessSnapshot& freshness,
                              const RerankPolicy& policy) const {
  if (!candidates.has_consistent_values()) {
    RankedHitSet empty;
    empty.degraded = true;
    return empty;
  }

  if (!freshness.has_consistent_values() || !policy.has_consistent_values()) {
    return fallback_lexical_first(candidates, freshness);
  }

  if (candidates.sparse_hits.empty() && candidates.dense_hits.empty()) {
    return RankedHitSet{
        .hits = {},
        .degraded = candidates.degraded,
    };
  }

  std::vector<AggregateHit> aggregates;
  add_lane_hits(candidates.sparse_hits, true, policy.rrf_k, aggregates);
  add_lane_hits(candidates.dense_hits, false, policy.rrf_k, aggregates);

  const auto max_raw_rrf_score = std::max_element(aggregates.begin(),
                                                  aggregates.end(),
                                                  [](const auto& left, const auto& right) {
                                                    return left.raw_rrf_score < right.raw_rrf_score;
                                                  })->raw_rrf_score;

  const bool stale = freshness.state == FreshnessState::StaleAllowed && freshness.stale_read_allowed;
  RankedHitSet ranked_hit_set;
  ranked_hit_set.degraded = candidates.degraded;

  for (const auto& aggregate : aggregates) {
    const auto normalized_rrf_score = max_raw_rrf_score > 0.0F
                                          ? aggregate.raw_rrf_score / max_raw_rrf_score
                                          : 0.0F;
    float fused_score = normalized_rrf_score;
    fused_score *= authority_multiplier(policy, aggregate.hit.authority_level);
    if (stale) {
      fused_score *= policy.stale_penalty_factor;
    }
    fused_score = clamp_score(fused_score);

    RankedHit ranked_hit{
        .hit = aggregate.hit,
        .fused_score = fused_score,
        .stale = stale,
        .score_reason_codes = {},
    };

    if (aggregate.seen_sparse && aggregate.seen_dense) {
      append_reason_code(ranked_hit.score_reason_codes, "rrf_multi_lane");
    } else if (aggregate.seen_sparse) {
      append_reason_code(ranked_hit.score_reason_codes, "rrf_sparse_only");
    } else if (aggregate.seen_dense) {
      append_reason_code(ranked_hit.score_reason_codes, "rrf_dense_only");
    }

    switch (aggregate.hit.authority_level) {
      case AuthorityLevel::Normative:
        append_reason_code(ranked_hit.score_reason_codes, "authority_boost_normative");
        break;
      case AuthorityLevel::Reference:
        append_reason_code(ranked_hit.score_reason_codes, "authority_weight_reference");
        break;
      case AuthorityLevel::Advisory:
        append_reason_code(ranked_hit.score_reason_codes, "authority_penalty_advisory");
        break;
    }

    if (stale) {
      append_reason_code(ranked_hit.score_reason_codes, "stale_penalty_applied");
    }

    ranked_hit_set.hits.push_back(std::move(ranked_hit));
  }

  std::sort(ranked_hit_set.hits.begin(),
            ranked_hit_set.hits.end(),
            [](const RankedHit& left, const RankedHit& right) {
              if (left.fused_score != right.fused_score) {
                return left.fused_score > right.fused_score;
              }
              return left.hit.chunk_id < right.hit.chunk_id;
            });

  ranked_hit_set.hits.erase(
      std::remove_if(ranked_hit_set.hits.begin(),
                     ranked_hit_set.hits.end(),
                     [&](const RankedHit& hit) { return hit.fused_score < policy.min_score_cutoff; }),
      ranked_hit_set.hits.end());

  if (ranked_hit_set.hits.size() > policy.top_k) {
    ranked_hit_set.hits.resize(policy.top_k);
  }

  return ranked_hit_set;
}

}  // namespace dasall::knowledge::rerank
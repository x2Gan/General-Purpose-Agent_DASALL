#include "query/CorpusRouter.h"

#include <algorithm>

namespace dasall::knowledge::query {

namespace {

void append_reason_code(std::vector<std::string>& reason_codes, std::string reason_code) {
  if (std::find(reason_codes.begin(), reason_codes.end(), reason_code) == reason_codes.end()) {
    reason_codes.push_back(std::move(reason_code));
  }
}

[[nodiscard]] bool supports_lexical(const CorpusDescriptor& descriptor) {
  return std::find(descriptor.supported_modes.begin(),
                   descriptor.supported_modes.end(),
                   RetrievalMode::LexicalOnly) != descriptor.supported_modes.end() ||
         std::find(descriptor.supported_modes.begin(),
                   descriptor.supported_modes.end(),
                   RetrievalMode::Hybrid) != descriptor.supported_modes.end();
}

[[nodiscard]] bool supports_dense(const CorpusDescriptor& descriptor) {
  return std::find(descriptor.supported_modes.begin(),
                   descriptor.supported_modes.end(),
                   RetrievalMode::DenseOnly) != descriptor.supported_modes.end() ||
         std::find(descriptor.supported_modes.begin(),
                   descriptor.supported_modes.end(),
                   RetrievalMode::Hybrid) != descriptor.supported_modes.end();
}

[[nodiscard]] bool matches_allowed_corpora(const NormalizedQuery& query,
                                           const CorpusDescriptor& descriptor) {
  return query.allowed_corpora.empty() ||
         std::find(query.allowed_corpora.begin(),
                   query.allowed_corpora.end(),
                   descriptor.corpus_id) != query.allowed_corpora.end();
}

[[nodiscard]] bool matches_minimum_authority(KnowledgeQueryKind query_kind,
                                             const CorpusDescriptor& descriptor) {
  AuthorityLevel minimum_authority = AuthorityLevel::Reference;
  switch (query_kind) {
    case KnowledgeQueryKind::PolicyEvidence:
      minimum_authority = AuthorityLevel::Normative;
      break;
    case KnowledgeQueryKind::DiagnosticContext:
      minimum_authority = AuthorityLevel::Advisory;
      break;
    case KnowledgeQueryKind::FactLookup:
    case KnowledgeQueryKind::ProcedureLookup:
      minimum_authority = AuthorityLevel::Reference;
      break;
    case KnowledgeQueryKind::MultiHop:
      minimum_authority = AuthorityLevel::Normative;
      break;
  }

  return static_cast<int>(descriptor.authority_level) <= static_cast<int>(minimum_authority);
}

[[nodiscard]] bool all_dense_capable(const std::vector<CorpusDescriptor>& candidates) {
  return !candidates.empty() &&
         std::all_of(candidates.begin(), candidates.end(), [](const CorpusDescriptor& descriptor) {
           return supports_dense(descriptor);
         });
}

[[nodiscard]] bool all_lexical_capable(const std::vector<CorpusDescriptor>& candidates) {
  return !candidates.empty() && std::all_of(candidates.begin(),
                                            candidates.end(),
                                            [](const CorpusDescriptor& descriptor) {
                                              return supports_lexical(descriptor);
                                            });
}

[[nodiscard]] bool prefers_dense_only(KnowledgeQueryKind query_kind) {
  return query_kind == KnowledgeQueryKind::DiagnosticContext;
}

[[nodiscard]] bool prefers_hybrid(KnowledgeQueryKind query_kind) {
  switch (query_kind) {
    case KnowledgeQueryKind::FactLookup:
    case KnowledgeQueryKind::ProcedureLookup:
    case KnowledgeQueryKind::PolicyEvidence:
      return true;
    case KnowledgeQueryKind::DiagnosticContext:
    case KnowledgeQueryKind::MultiHop:
      return false;
  }

  return false;
}

[[nodiscard]] RoutePlanResult make_route_error(KnowledgeErrorCode code,
                                               std::string message,
                                               std::vector<std::string> reason_codes) {
  RoutePlanResult result;
  result.ok = false;
  result.route_reason_codes = std::move(reason_codes);
  result.error = make_knowledge_error_info(code,
                                           std::move(message),
                                           "corpus_router.build_plan");
  return result;
}

[[nodiscard]] std::vector<std::string> collect_corpus_ids(
    const std::vector<CorpusDescriptor>& descriptors) {
  std::vector<std::string> corpus_ids;
  corpus_ids.reserve(descriptors.size());
  for (const auto& descriptor : descriptors) {
    corpus_ids.push_back(descriptor.corpus_id);
  }
  return corpus_ids;
}

}  // namespace

bool RetrievalPlan::has_consistent_values() const {
  if (!detail::has_unique_values(corpus_ids) || !detail::has_unique_values(route_reason_codes) ||
      corpus_ids.empty() || max_projection_items == 0U) {
    return false;
  }

  switch (mode) {
    case RetrievalMode::LexicalOnly:
      return sparse_top_k > 0U && dense_top_k == 0U;
    case RetrievalMode::DenseOnly:
      return sparse_top_k == 0U && dense_top_k > 0U;
    case RetrievalMode::Hybrid:
      return sparse_top_k > 0U && dense_top_k > 0U;
  }

  return false;
}

bool RoutePlanResult::has_consistent_values() const {
  if (!detail::has_unique_values(route_reason_codes)) {
    return false;
  }

  if (ok) {
    return plan.has_value() && plan->has_consistent_values() && !error.has_value() &&
           plan->route_reason_codes == route_reason_codes;
  }

  return !plan.has_value() && !route_reason_codes.empty() && error.has_value() &&
         detail::has_error_shape(error);
}

RoutePlanResult CorpusRouter::build_plan(const NormalizedQuery& query,
                                         const KnowledgeConfigSnapshot& config,
                                         const index::CorpusCatalogSnapshot& catalog,
                                         const FreshnessSnapshot& freshness) const {
  if (!query.has_consistent_values() || !config.has_consistent_values() ||
      !catalog.has_consistent_values() || !freshness.has_consistent_values()) {
    return make_route_error(KnowledgeErrorCode::InternalError,
                            "corpus router inputs are inconsistent",
                            {"router_input_inconsistent"});
  }

  if (!config.knowledge_enabled) {
    return make_route_error(KnowledgeErrorCode::Disabled,
                            "knowledge module is disabled by configuration",
                            {"knowledge_disabled"});
  }

  std::vector<std::string> route_reason_codes;
  auto candidates = query.domain_tags.empty() ? catalog.list_all() : catalog.filter_by_tags(query.domain_tags);
  if (!query.domain_tags.empty()) {
    append_reason_code(route_reason_codes, "domain_tag_filter_applied");
  }

  candidates.erase(std::remove_if(candidates.begin(),
                                  candidates.end(),
                                  [](const CorpusDescriptor& descriptor) {
                                    return descriptor.trust_level != TrustLevel::Trusted;
                                  }),
                   candidates.end());
  append_reason_code(route_reason_codes, "trusted_corpus_filter_applied");

  if (!query.allowed_corpora.empty()) {
    candidates.erase(std::remove_if(candidates.begin(),
                                    candidates.end(),
                                    [&](const CorpusDescriptor& descriptor) {
                                      return !matches_allowed_corpora(query, descriptor);
                                    }),
                     candidates.end());
    append_reason_code(route_reason_codes, "allowed_corpora_filter_applied");
  }

  candidates.erase(std::remove_if(candidates.begin(),
                                  candidates.end(),
                                  [&](const CorpusDescriptor& descriptor) {
                                    return !matches_minimum_authority(query.query_kind, descriptor);
                                  }),
                   candidates.end());
  append_reason_code(route_reason_codes, "authority_filter_applied");

  if (candidates.empty()) {
    append_reason_code(route_reason_codes, "no_corpus_available");
    return make_route_error(KnowledgeErrorCode::NoCorpusAvailable,
                            "no corpus satisfies corpus, tag, trust, and authority filters",
                            std::move(route_reason_codes));
  }

  switch (freshness.state) {
    case FreshnessState::Fresh:
      append_reason_code(route_reason_codes, "fresh_snapshot_required");
      break;
    case FreshnessState::StaleAllowed:
      if (!freshness.stale_read_allowed || !query.allow_stale) {
        append_reason_code(route_reason_codes, "stale_snapshot_rejected");
        return make_route_error(KnowledgeErrorCode::IndexStaleRejected,
                                "stale snapshot is not allowed for this query",
                                std::move(route_reason_codes));
      }
      append_reason_code(route_reason_codes, "stale_snapshot_allowed");
      break;
    case FreshnessState::StaleRejected:
      append_reason_code(route_reason_codes, "stale_snapshot_rejected");
      return make_route_error(KnowledgeErrorCode::IndexStaleRejected,
                              "current snapshot is stale and rejected by policy",
                              std::move(route_reason_codes));
    case FreshnessState::Unknown:
      append_reason_code(route_reason_codes, "freshness_unknown");
      return make_route_error(KnowledgeErrorCode::IndexStaleRejected,
                              "snapshot freshness is unknown and cannot be routed",
                              std::move(route_reason_codes));
  }

  const auto dense_capable = all_dense_capable(candidates);
  const auto lexical_capable = all_lexical_capable(candidates);
  const auto mode = select_mode(query, config, candidates);

  switch (mode) {
    case RetrievalMode::LexicalOnly:
      append_reason_code(route_reason_codes, "mode_lexical_only");
      if (!config.vector_enabled) {
        append_reason_code(route_reason_codes, "vector_disabled_lexical_fallback");
      } else if ((prefers_dense_only(query.query_kind) || prefers_hybrid(query.query_kind)) &&
                 !dense_capable) {
        append_reason_code(route_reason_codes, "corpus_mode_capability_downgraded");
      } else if (config.retrieval_mode_default == RetrievalMode::LexicalOnly) {
        append_reason_code(route_reason_codes, "profile_forced_lexical_only");
      }
      break;
    case RetrievalMode::DenseOnly:
      append_reason_code(route_reason_codes, "mode_dense_only");
      break;
    case RetrievalMode::Hybrid:
      append_reason_code(route_reason_codes, "mode_hybrid");
      break;
  }

  RoutePlanResult result;
  result.ok = true;
  result.route_reason_codes = route_reason_codes;
  result.plan = RetrievalPlan{
      .mode = mode,
      .corpus_ids = collect_corpus_ids(candidates),
      .sparse_top_k = mode == RetrievalMode::DenseOnly ? 0U : query.top_k,
      .dense_top_k = mode == RetrievalMode::LexicalOnly ? 0U : query.top_k,
      .allow_partial_results = mode == RetrievalMode::Hybrid && config.allow_budget_degrade,
      .allow_stale_snapshot = freshness.state == FreshnessState::StaleAllowed,
      .max_projection_items = query.max_context_projection_items,
      .route_reason_codes = route_reason_codes,
  };

  if (result.plan->allow_partial_results) {
    append_reason_code(result.route_reason_codes, "partial_results_allowed");
    result.plan->route_reason_codes = result.route_reason_codes;
  }

  if (mode == RetrievalMode::DenseOnly && !dense_capable) {
    return make_route_error(KnowledgeErrorCode::NoCorpusAvailable,
                            "dense-only route selected without any dense-capable corpus",
                            {"dense_route_unavailable"});
  }

  if (mode == RetrievalMode::LexicalOnly && !lexical_capable) {
    return make_route_error(KnowledgeErrorCode::NoCorpusAvailable,
                            "lexical route selected without any lexical-capable corpus",
                            {"lexical_route_unavailable"});
  }

  return result;
}

RetrievalMode CorpusRouter::select_mode(const NormalizedQuery& query,
                                        const KnowledgeConfigSnapshot& config,
                                        const std::vector<CorpusDescriptor>& candidates) const {
  if (!config.vector_enabled || config.retrieval_mode_default == RetrievalMode::LexicalOnly) {
    return RetrievalMode::LexicalOnly;
  }

  const auto dense_capable = all_dense_capable(candidates);
  const auto lexical_capable = all_lexical_capable(candidates);

  if (prefers_dense_only(query.query_kind) && dense_capable) {
    return RetrievalMode::DenseOnly;
  }

  if (prefers_hybrid(query.query_kind) && dense_capable && lexical_capable) {
    return RetrievalMode::Hybrid;
  }

  if (config.retrieval_mode_default == RetrievalMode::DenseOnly && dense_capable) {
    return RetrievalMode::DenseOnly;
  }

  if (config.retrieval_mode_default == RetrievalMode::Hybrid && dense_capable && lexical_capable) {
    return RetrievalMode::Hybrid;
  }

  return RetrievalMode::LexicalOnly;
}

}  // namespace dasall::knowledge::query
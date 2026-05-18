#include "facade/KnowledgeService.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace dasall::knowledge::facade {

namespace {

[[nodiscard]] std::string error_category_name(const std::optional<dasall::contracts::ErrorInfo>& error) {
  if (!error.has_value() || !error->failure_type.has_value()) {
    return "none";
  }

  switch (*error->failure_type) {
    case dasall::contracts::ResultCodeCategory::Validation:
      return "validation";
    case dasall::contracts::ResultCodeCategory::Policy:
      return "policy";
    case dasall::contracts::ResultCodeCategory::Provider:
      return "provider";
    case dasall::contracts::ResultCodeCategory::Runtime:
      return "runtime";
    case dasall::contracts::ResultCodeCategory::Tool:
      return "tool";
    case dasall::contracts::ResultCodeCategory::Unknown:
      break;
  }

  return "unknown";
}

[[nodiscard]] KnowledgeErrorCode map_recall_failure(
    const std::vector<std::string>& failure_reason_codes) {
  if (std::any_of(failure_reason_codes.begin(), failure_reason_codes.end(), [](const std::string& code) {
        return code.find("recall_timeout") != std::string::npos;
      })) {
    return KnowledgeErrorCode::RecallTimeout;
  }

  if (std::any_of(failure_reason_codes.begin(), failure_reason_codes.end(), [](const std::string& code) {
        return code.find("index_unavailable") != std::string::npos;
      })) {
    return KnowledgeErrorCode::IndexUnavailable;
  }

  if (std::any_of(failure_reason_codes.begin(), failure_reason_codes.end(), [](const std::string& code) {
        return code.find("dense_lane_unavailable") != std::string::npos ||
               code.find("vector_backend_unavailable") != std::string::npos;
      })) {
    return KnowledgeErrorCode::VectorBackendUnavailable;
  }

  return KnowledgeErrorCode::InternalError;
}

[[nodiscard]] std::optional<std::string> derive_required_language(
    const KnowledgeQuery& query) {
  (void)query;
  return std::nullopt;
}

[[nodiscard]] rerank::RerankPolicy make_rerank_policy(const KnowledgeQuery& query) {
  rerank::RerankPolicy policy;
  policy.top_k = query.top_k;
  return policy;
}

[[nodiscard]] RefreshResult make_busy_refresh_result() {
  RefreshResult result;
  result.status = RefreshStatus::Busy;
  return result;
}

[[nodiscard]] RefreshResult make_refresh_failure_result(KnowledgeErrorCode error_code,
                                                        std::string_view message,
                                                        std::string_view ref_id) {
  RefreshResult result;
  result.status = RefreshStatus::Failed;
  result.error = make_knowledge_error_info(error_code,
                                           std::string(message),
                                           "knowledge_service_facade.request_refresh",
                                           std::string(ref_id));
  return result;
}

[[nodiscard]] std::string collapse_whitespace(std::string_view text) {
  std::string collapsed;
  collapsed.reserve(text.size());
  bool last_was_space = false;
  for (const char raw_character : text) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (std::isspace(character) != 0) {
      if (!collapsed.empty() && !last_was_space) {
        collapsed.push_back(' ');
      }
      last_was_space = true;
      continue;
    }

    collapsed.push_back(static_cast<char>(character));
    last_was_space = false;
  }

  if (!collapsed.empty() && collapsed.back() == ' ') {
    collapsed.pop_back();
  }
  return collapsed;
}

[[nodiscard]] std::string source_kind_name(const SourceKind source_kind) {
  switch (source_kind) {
    case SourceKind::File:
      return "file";
    case SourceKind::ConfigSnapshot:
      return "config_snapshot";
    case SourceKind::CuratedBundle:
      return "curated_bundle";
  }

  return "file";
}

[[nodiscard]] std::string trust_level_name(const TrustLevel trust_level) {
  switch (trust_level) {
    case TrustLevel::Trusted:
      return "trusted";
    case TrustLevel::Quarantined:
      return "quarantined";
    case TrustLevel::Unregistered:
      return "unregistered";
  }

  return "trusted";
}

[[nodiscard]] std::string freshness_name(const FreshnessState freshness) {
  switch (freshness) {
    case FreshnessState::Fresh:
      return "fresh";
    case FreshnessState::StaleAllowed:
      return "stale_allowed";
    case FreshnessState::StaleRejected:
      return "stale_rejected";
    case FreshnessState::Unknown:
      return "unknown";
  }

  return "unknown";
}

[[nodiscard]] std::optional<std::string> anchor_locator_from_citation_ref(
    std::string_view citation_ref) {
  const auto anchor_begin = citation_ref.find('#');
  if (anchor_begin == std::string_view::npos ||
      anchor_begin + 1U >= citation_ref.size()) {
    return std::nullopt;
  }

  return std::string(citation_ref.substr(anchor_begin + 1U));
}

[[nodiscard]] std::vector<contracts::RetrievalEvidenceRef> build_retrieval_evidence_refs(
    const rerank::RankedHitSet& ranked_hits,
    const EvidenceBundle& evidence,
    const index::CorpusCatalogSnapshot& catalog) {
  std::vector<contracts::RetrievalEvidenceRef> refs;
  refs.reserve(evidence.slices.size());

  std::vector<std::string> seen_chunk_ids;
  std::size_t slice_index = 0U;
  for (const auto& ranked_hit : ranked_hits.hits) {
    if (slice_index >= evidence.slices.size()) {
      break;
    }

    if (!ranked_hit.has_consistent_values()) {
      continue;
    }

    if (std::find(seen_chunk_ids.begin(),
                  seen_chunk_ids.end(),
                  ranked_hit.hit.chunk_id) != seen_chunk_ids.end()) {
      continue;
    }

    if (collapse_whitespace(ranked_hit.hit.raw_snippet).empty()) {
      continue;
    }

    seen_chunk_ids.push_back(ranked_hit.hit.chunk_id);

    const auto descriptor = catalog.find_by_id(ranked_hit.hit.corpus_id);
    const auto& slice = evidence.slices[slice_index++];
    if (!descriptor.has_value()) {
      continue;
    }

    contracts::RetrievalEvidenceRef ref;
    ref.evidence_ref = slice.evidence_id;
    ref.source_ref = slice.citation_ref;
    ref.source_kind = source_kind_name(descriptor->source_kind);
    ref.summary_text = slice.snippet;
    ref.trust_level = trust_level_name(descriptor->trust_level);
    ref.freshness = freshness_name(slice.freshness);
    ref.anchor_locator = anchor_locator_from_citation_ref(slice.citation_ref);
    if (ref.has_consistent_values()) {
      refs.push_back(std::move(ref));
    }
  }

  return refs;
}

}  // namespace

bool StageBudget::has_consistent_values() const {
  return absolute_deadline_ms > 0 && normalize_route_ms > 0 && sparse_recall_ms > 0 &&
         dense_recall_ms > 0 && rerank_evidence_ms > 0 && telemetry_wrap_ms > 0;
}

KnowledgeServiceFacade::KnowledgeServiceFacade(KnowledgeServiceDeps deps)
    : deps_(std::move(deps)) {
  bind_default_component_seams();
}

KnowledgeServiceFacade::~KnowledgeServiceFacade() {
  join_previous_refresh_worker();
}

bool KnowledgeServiceFacade::init(const KnowledgeConfigSnapshot& config) {
  if (!config.has_consistent_values()) {
    return false;
  }

  config_ = config;
  lifecycle_state_ = LifecycleState::Running;
  return true;
}

KnowledgeRetrieveResult KnowledgeServiceFacade::retrieve(const KnowledgeQuery& query) {
  if (!query.has_consistent_values()) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::QueryValidationFailed,
                       "query_inconsistent");
  }

  if (lifecycle_state_ != LifecycleState::Running) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::NotInitialized,
                       "knowledge_not_initialized");
  }

  if (!config_.knowledge_enabled) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::Disabled,
                       "knowledge_disabled");
  }

  const auto stage_budget = compute_stage_budget(config_.request_deadline_ms);
  if (!stage_budget.has_consistent_values()) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "stage_budget_inconsistent");
  }

  if (!deps_.normalize_query) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "normalize_query_missing");
  }

  auto normalize_result = deps_.normalize_query(query);
  if (!normalize_result.has_consistent_values()) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "normalize_result_inconsistent");
  }

  if (!normalize_result.ok || !normalize_result.normalized_query.has_value()) {
    KnowledgeRetrieveResult result;
    result.ok = false;
    result.mode = RetrievalMode::LexicalOnly;
    result.error = normalize_result.error;
    return result;
  }

  if (now_ms() >= stage_budget.absolute_deadline_ms) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::RecallTimeout,
                       "deadline_exceeded_after_normalize");
  }

  const auto manifest = deps_.current_manifest ? deps_.current_manifest() : std::nullopt;
  if (manifest.has_value() && !manifest->has_consistent_values()) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::IndexUnavailable,
                       "manifest_inconsistent");
  }

  if (!deps_.evaluate_freshness) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "freshness_evaluator_missing");
  }

  auto freshness = deps_.evaluate_freshness(manifest, config_, now_ms(), query.allow_stale);
  if (!freshness.has_consistent_values()) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "freshness_inconsistent");
  }

  if (!deps_.build_plan) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "router_missing");
  }

  const auto catalog = deps_.catalog_snapshot ? deps_.catalog_snapshot() : index::CorpusCatalogSnapshot();
  const auto route_result = deps_.build_plan(*normalize_result.normalized_query, config_, catalog, freshness);
  if (!route_result.has_consistent_values()) {
    return fail_closed(query, RetrievalMode::LexicalOnly,
                       KnowledgeErrorCode::InternalError,
                       "route_result_inconsistent");
  }

  if (!route_result.ok || !route_result.plan.has_value()) {
    KnowledgeRetrieveResult result;
    result.ok = false;
    result.mode = RetrievalMode::LexicalOnly;
    result.error = route_result.error;
    return result;
  }

  if (now_ms() >= stage_budget.absolute_deadline_ms) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::RecallTimeout,
                       "deadline_exceeded_after_route");
  }

  if (!deps_.recall) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::InternalError,
                       "recall_missing");
  }

  retrieve::RecallRequest recall_request;
  recall_request.normalized_query = *normalize_result.normalized_query;
  recall_request.plan = *route_result.plan;
  recall_request.required_language = derive_required_language(query);
  auto recall_result = deps_.recall(recall_request);
  if (!recall_result.has_consistent_values()) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::InternalError,
                       "recall_result_inconsistent");
  }

  if (!recall_result.ok) {
    return fail_closed(query, route_result.plan->mode,
                       map_recall_failure(recall_result.failure_reason_codes),
                       recall_result.failure_reason_codes.empty()
                           ? std::string_view("recall_failed")
                           : std::string_view(recall_result.failure_reason_codes.front()));
  }

  if (now_ms() >= stage_budget.absolute_deadline_ms) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::RecallTimeout,
                       "deadline_exceeded_after_recall");
  }

  if (!deps_.rerank) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::InternalError,
                       "rerank_missing");
  }

  auto ranked_hits = deps_.rerank(recall_result.candidates, freshness, make_rerank_policy(query));
  if (!ranked_hits.has_consistent_values()) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::InternalError,
                       "rerank_result_inconsistent");
  }

  if (!deps_.assemble_evidence) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::InternalError,
                       "assembler_missing");
  }

  auto evidence = deps_.assemble_evidence(
      ranked_hits, evidence::EvidenceAssemblePolicy::from_query(query, config_));
  if (!evidence.has_consistent_values()) {
    return fail_closed(query, route_result.plan->mode,
                       KnowledgeErrorCode::InternalError,
                       "evidence_inconsistent");
  }

  evidence.degraded = evidence.degraded || recall_result.candidates.degraded || ranked_hits.degraded;

  KnowledgeRetrieveResult result;
  result.ok = true;
  result.mode = route_result.plan->mode;
  result.evidence = std::move(evidence);
  result.retrieval_evidence_refs = build_retrieval_evidence_refs(
      ranked_hits,
      *result.evidence,
      catalog);

  if (deps_.emit_retrieve_event) {
    KnowledgeTelemetryEvent event;
    event.event_name = "knowledge_retrieve";
    event.request_id = query.request_id;
    event.component = "knowledge_service_facade";
    event.snapshot_id = manifest.has_value() ? manifest->snapshot_id : "none";
    event.result = "success";
    event.degraded = result.evidence->degraded;
    event.latency_ms = std::max<std::int64_t>(
        0, now_ms() - (stage_budget.absolute_deadline_ms - config_.request_deadline_ms));
    event.reason_codes = route_result.route_reason_codes;
    event.corpus_ids = route_result.plan->corpus_ids;
    event.query_kind = query.query_kind;
    event.retrieval_mode = route_result.plan->mode;
    event.corpus_count = route_result.plan->corpus_ids.size();
    event.result_count = result.evidence->slices.size();
    event.error_category = "none";
    deps_.emit_retrieve_event(event);
  }

  return result;
}

KnowledgeHealthSnapshot KnowledgeServiceFacade::health_snapshot() const {
  KnowledgeHealthSnapshot snapshot;
  if (deps_.collect_health_snapshot) {
    snapshot = deps_.collect_health_snapshot();
  } else {
    snapshot.state = HealthState::Unknown;
    snapshot.reason_codes = {"health_probe_unavailable"};
  }

  snapshot.refresh_in_flight = refresh_in_flight_.load();
  const auto last_refresh_status_code = last_refresh_status_code_.load();
  if (last_refresh_status_code >= 0) {
    snapshot.last_refresh_status =
        static_cast<RefreshStatus>(last_refresh_status_code);
  }
  return snapshot;
}

RefreshResult KnowledgeServiceFacade::request_refresh(const CorpusChangeSet& changes) {
  if (refresh_in_flight_.exchange(true)) {
    return make_busy_refresh_result();
  }

  if (deps_.request_refresh) {
    auto refresh_result = run_refresh_delegate(changes, true);
    refresh_in_flight_.store(false);
    if (refresh_result.status != RefreshStatus::Busy) {
      last_refresh_status_code_.store(static_cast<int>(refresh_result.status));
    }
    return refresh_result;
  }

  if (!deps_.ingestion_coordinator || !deps_.index_writer) {
    refresh_in_flight_.store(false);
    return make_busy_refresh_result();
  }

  join_previous_refresh_worker();

  const auto refresh_id = next_refresh_job_id();

  try {
    refresh_worker_ = std::thread([this, changes]() {
      auto refresh_result = run_refresh_delegate(changes, false);
      last_refresh_status_code_.store(static_cast<int>(refresh_result.status));
      refresh_in_flight_.store(false);
    });
  } catch (const std::exception& exception) {
    refresh_in_flight_.store(false);
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       exception.what(),
                                       "refresh_worker_launch_failed");
  }

  RefreshResult refresh_result;
  refresh_result.status = RefreshStatus::Accepted;
  refresh_result.refresh_id = refresh_id;
  return refresh_result;
}

RefreshResult KnowledgeServiceFacade::request_refresh_sync_for_tests(
    const CorpusChangeSet& changes) {
  if (refresh_in_flight_.exchange(true)) {
    return make_busy_refresh_result();
  }

  join_previous_refresh_worker();

  auto refresh_result = run_refresh_delegate(changes, true);
  refresh_in_flight_.store(false);
  if (refresh_result.status != RefreshStatus::Busy) {
    last_refresh_status_code_.store(static_cast<int>(refresh_result.status));
  }
  return refresh_result;
}

RefreshResult KnowledgeServiceFacade::run_refresh_delegate(const CorpusChangeSet& changes,
                                                           const bool allow_busy_result) {
  RefreshResult refresh_result;
  try {
    refresh_result = deps_.request_refresh ? deps_.request_refresh(changes)
                                           : run_real_refresh(changes);
  } catch (const std::exception& exception) {
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       exception.what(),
                                       "refresh_exception");
  } catch (...) {
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       "refresh raised an unknown exception",
                                       "refresh_exception");
  }

  if (!refresh_result.has_consistent_values()) {
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       "refresh result is inconsistent",
                                       "refresh_result_inconsistent");
  }

  if (!allow_busy_result && refresh_result.status == RefreshStatus::Busy) {
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       "refresh worker returned busy",
                                       "refresh_worker_busy");
  }

  return refresh_result;
}

std::string KnowledgeServiceFacade::next_refresh_job_id() {
  const auto sequence = refresh_job_sequence_.fetch_add(1U) + 1U;
  return "refresh-job:" + std::to_string(now_ms()) + ":" + std::to_string(sequence);
}

void KnowledgeServiceFacade::join_previous_refresh_worker() {
  if (refresh_worker_.joinable()) {
    refresh_worker_.join();
  }
}

StageBudget KnowledgeServiceFacade::compute_stage_budget(std::int64_t deadline_ms) const {
  StageBudget budget;
  budget.absolute_deadline_ms = now_ms() + deadline_ms;
  budget.normalize_route_ms = deadline_ms * 5 / 100;
  budget.sparse_recall_ms = deadline_ms * 35 / 100;
  budget.dense_recall_ms = deadline_ms * 35 / 100;
  budget.rerank_evidence_ms = deadline_ms * 15 / 100;
  budget.telemetry_wrap_ms = deadline_ms - budget.normalize_route_ms - budget.sparse_recall_ms -
                             budget.dense_recall_ms - budget.rerank_evidence_ms;
  return budget;
}

KnowledgeRetrieveResult KnowledgeServiceFacade::fail_closed(const KnowledgeQuery& query,
                                                            RetrievalMode mode,
                                                            KnowledgeErrorCode error_code,
                                                            std::string_view reason) const {
  KnowledgeRetrieveResult result;
  result.ok = false;
  result.mode = mode;
  result.error = make_knowledge_error_info(error_code, std::string(reason),
                                           "knowledge_service_facade.retrieve",
                                           std::string(reason));

  if (deps_.emit_retrieve_event) {
    KnowledgeTelemetryEvent event;
    event.event_name = "knowledge_retrieve";
    event.request_id = query.request_id;
    event.component = "knowledge_service_facade";
    event.snapshot_id = "none";
    event.result = "failure";
    event.latency_ms = 0;
    event.query_kind = query.query_kind;
    event.retrieval_mode = mode;
    event.error_category = error_category_name(result.error);
    deps_.emit_retrieve_event(event);
  }

  return result;
}

void KnowledgeServiceFacade::bind_default_component_seams() {
  if (!deps_.normalize_query && deps_.query_normalizer) {
    deps_.normalize_query = [this](const KnowledgeQuery& query) {
      return deps_.query_normalizer->normalize(query);
    };
  }

  if (!deps_.catalog_snapshot && deps_.corpus_catalog) {
    deps_.catalog_snapshot = [this] {
      return deps_.corpus_catalog->snapshot();
    };
  }

  if (!deps_.current_manifest && deps_.index_reader) {
    deps_.current_manifest = [this] {
      return deps_.index_reader->current_manifest();
    };
  }

  if (!deps_.evaluate_freshness && deps_.freshness_controller) {
    deps_.evaluate_freshness = [this](const std::optional<IndexManifest>& manifest,
                                      const KnowledgeConfigSnapshot& config,
                                      std::int64_t current_now_ms,
                                      bool query_allow_stale) {
      return deps_.freshness_controller->evaluate(manifest,
                                                  config,
                                                  current_now_ms,
                                                  query_allow_stale);
    };
  }

  if (!deps_.build_plan && deps_.corpus_router) {
    deps_.build_plan = [this](const query::NormalizedQuery& query,
                              const KnowledgeConfigSnapshot& config,
                              const index::CorpusCatalogSnapshot& catalog,
                              const FreshnessSnapshot& freshness) {
      return deps_.corpus_router->build_plan(query, config, catalog, freshness);
    };
  }

  if (!deps_.recall && deps_.recall_coordinator) {
    deps_.recall = [this](const retrieve::RecallRequest& request) {
      return deps_.recall_coordinator->recall(request);
    };
  }

  if (!deps_.rerank && deps_.reranker) {
    deps_.rerank = [this](const retrieve::RecallCandidateSet& candidates,
                          const FreshnessSnapshot& freshness,
                          const rerank::RerankPolicy& policy) {
      return deps_.reranker->rerank(candidates, freshness, policy);
    };
  }

  if (!deps_.assemble_evidence && deps_.evidence_assembler) {
    deps_.assemble_evidence = [this](const rerank::RankedHitSet& hits,
                                     const evidence::EvidenceAssemblePolicy& policy) {
      return deps_.evidence_assembler->assemble(hits, policy);
    };
  }

  if (!deps_.collect_health_snapshot && deps_.health_probe) {
    deps_.collect_health_snapshot = [this] {
      return deps_.health_probe->collect();
    };
  }
}

std::int64_t KnowledgeServiceFacade::now_ms() const {
  if (deps_.now_ms) {
    return deps_.now_ms();
  }

  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

RefreshResult KnowledgeServiceFacade::run_real_refresh(const CorpusChangeSet& changes) {
  if (!changes.has_consistent_values()) {
    return make_refresh_failure_result(KnowledgeErrorCode::QueryValidationFailed,
                                       "corpus change set is inconsistent",
                                       "corpus_change_set_inconsistent");
  }

  const auto batch = deps_.ingestion_coordinator->build_update_batch(changes);
  if (!batch.has_consistent_values()) {
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       "ingestion batch is inconsistent",
                                       "index_update_batch_inconsistent");
  }

  const auto report = deps_.index_writer->apply_update_batch(batch);
  if (!report.has_consistent_values()) {
    return make_refresh_failure_result(KnowledgeErrorCode::RefreshFailed,
                                       "index writer report is inconsistent",
                                       "update_report_inconsistent");
  }

  if (!report.ok) {
    RefreshResult result;
    result.status = RefreshStatus::Failed;
    result.error = report.error.has_value()
                       ? report.error
                       : std::optional<dasall::contracts::ErrorInfo>(
                             make_knowledge_error_info(KnowledgeErrorCode::RefreshFailed,
                                                       "index writer failed to activate snapshot",
                                                       "knowledge_service_facade.request_refresh",
                                                       "index_writer_failed"));
    return result;
  }

  RefreshResult result;
  result.status = RefreshStatus::Completed;
  result.refresh_id = batch.batch_id;
  return result;
}

}  // namespace dasall::knowledge::facade
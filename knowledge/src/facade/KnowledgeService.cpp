#include "facade/KnowledgeService.h"

#include <algorithm>
#include <chrono>
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

[[nodiscard]] rerank::RerankPolicy make_rerank_policy(const KnowledgeQuery& query) {
  rerank::RerankPolicy policy;
  policy.top_k = query.top_k;
  return policy;
}

}  // namespace

bool StageBudget::has_consistent_values() const {
  return absolute_deadline_ms > 0 && normalize_route_ms > 0 && sparse_recall_ms > 0 &&
         dense_recall_ms > 0 && rerank_evidence_ms > 0 && telemetry_wrap_ms > 0;
}

KnowledgeServiceFacade::KnowledgeServiceFacade(KnowledgeServiceDeps deps)
    : deps_(std::move(deps)) {}

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
  recall_request.required_language = query.session_id.has_value() ? std::optional<std::string>("zh-CN") : std::nullopt;
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
  if (deps_.collect_health_snapshot) {
    return deps_.collect_health_snapshot();
  }

  KnowledgeHealthSnapshot snapshot;
  snapshot.state = HealthState::Unknown;
  snapshot.reason_codes = {"health_probe_unavailable"};
  return snapshot;
}

RefreshResult KnowledgeServiceFacade::request_refresh(const CorpusChangeSet& changes) {
  if (refresh_in_flight_.exchange(true)) {
    RefreshResult result;
    result.status = RefreshStatus::Busy;
    return result;
  }

  if (!deps_.request_refresh) {
    refresh_in_flight_.store(false);
    RefreshResult result;
    result.status = RefreshStatus::Busy;
    return result;
  }

  auto refresh_result = deps_.request_refresh(changes);
  refresh_in_flight_.store(false);

  if (!refresh_result.has_consistent_values()) {
    RefreshResult result;
    result.status = RefreshStatus::Failed;
    result.error = make_knowledge_error_info(KnowledgeErrorCode::RefreshFailed,
                                             "refresh result is inconsistent",
                                             "knowledge_service_facade.request_refresh",
                                             "refresh_result_inconsistent");
    return result;
  }

  return refresh_result;
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

std::int64_t KnowledgeServiceFacade::now_ms() const {
  if (deps_.now_ms) {
    return deps_.now_ms();
  }

  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace dasall::knowledge::facade
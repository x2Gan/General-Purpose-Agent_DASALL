#include <exception>
#include <iostream>
#include <vector>

#include "facade/KnowledgeService.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::facade::KnowledgeServiceDeps;
using dasall::knowledge::facade::KnowledgeServiceFacade;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] KnowledgeConfigSnapshot make_config() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = true;
  config.vector_enabled = false;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;
  config.evidence_budget_tokens = 256U;
  config.max_context_projection_items = 4U;
  config.catalog_refresh_interval_ms = 60000;
  config.catalog_expire_after_ms = 120000;
  config.failure_backoff_ms = 1000;
  config.request_deadline_ms = 1000;
  config.max_parallel_recall = 1U;
  config.sparse_recall_timeout_ms = 350;
  config.dense_recall_timeout_ms = 350;
  config.ingest_timeout_ms = 2000;
  return config;
}

[[nodiscard]] KnowledgeQuery make_query() {
  KnowledgeQuery query;
  query.request_id = "req-deadline";
  query.query_text = "policy evidence";
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

void test_facade_computes_fixed_stage_budget_distribution() {
  KnowledgeServiceDeps deps;
  deps.now_ms = [] { return 1000; };
  KnowledgeServiceFacade facade(std::move(deps));

  const auto budget = facade.compute_stage_budget(1000);
  assert_true(budget.has_consistent_values(),
              "stage budget should be internally consistent for a 1000ms deadline");
  assert_equal(2000, static_cast<int>(budget.absolute_deadline_ms),
               "absolute deadline should be based on now + request deadline");
  assert_equal(50, static_cast<int>(budget.normalize_route_ms),
               "normalize+route budget should consume 5 percent of the deadline");
  assert_equal(350, static_cast<int>(budget.sparse_recall_ms),
               "sparse recall budget should consume 35 percent of the deadline");
  assert_equal(350, static_cast<int>(budget.dense_recall_ms),
               "dense recall budget should consume 35 percent of the deadline");
  assert_equal(150, static_cast<int>(budget.rerank_evidence_ms),
               "rerank+evidence budget should consume 15 percent of the deadline");
  assert_equal(100, static_cast<int>(budget.telemetry_wrap_ms),
               "telemetry+wrap budget should consume the remaining 10 percent of the deadline");
}

void test_facade_fails_fast_when_deadline_is_exceeded_mid_pipeline() {
  std::vector<std::int64_t> clock_values = {1000, 1000, 1000, 2500};
  std::size_t clock_index = 0U;

  KnowledgeServiceDeps deps;
  deps.now_ms = [&clock_values, &clock_index] {
    const auto value = clock_values.at(clock_index < clock_values.size() ? clock_index
                                                                         : clock_values.size() - 1U);
    if (clock_index < clock_values.size() - 1U) {
      ++clock_index;
    }
    return value;
  };
  deps.normalize_query = [](const KnowledgeQuery& query) {
    dasall::knowledge::query::NormalizeResult result;
    dasall::knowledge::query::NormalizedQuery normalized_query;
    normalized_query.request_id = query.request_id;
    normalized_query.normalized_text = "policy evidence";
    normalized_query.lexical_terms = {"policy"};
    normalized_query.top_k = query.top_k;
    normalized_query.max_context_projection_items = query.max_context_projection_items;
    result.ok = true;
    result.normalized_query = std::move(normalized_query);
    return result;
  };
  deps.evaluate_freshness = [](const std::optional<dasall::knowledge::IndexManifest>&,
                               const KnowledgeConfigSnapshot&,
                               std::int64_t,
                               bool) {
    dasall::knowledge::FreshnessSnapshot snapshot;
    snapshot.state = dasall::knowledge::FreshnessState::Fresh;
    snapshot.age_ms = 10;
    return snapshot;
  };
  deps.build_plan = [](const dasall::knowledge::query::NormalizedQuery&,
                       const KnowledgeConfigSnapshot&,
                       const auto&,
                       const dasall::knowledge::FreshnessSnapshot&) {
    dasall::knowledge::query::RoutePlanResult result;
    result.ok = true;
    result.plan = dasall::knowledge::query::RetrievalPlan{
        .mode = RetrievalMode::LexicalOnly,
        .corpus_ids = {"adr_normative"},
        .sparse_top_k = 4U,
        .allow_partial_results = false,
        .max_projection_items = 4U,
        .route_reason_codes = {"route_ok"},
    };
    result.route_reason_codes = {"route_ok"};
    return result;
  };
  KnowledgeServiceFacade facade(std::move(deps));

  assert_true(facade.init(make_config()),
              "facade should initialize before deadline fail-fast test");
  const auto result = facade.retrieve(make_query());
  assert_true(!result.ok,
              "deadline exhaustion mid-pipeline should fail closed");
  assert_equal(static_cast<int>(KnowledgeErrorCode::RecallTimeout),
               result.error->details.code.value_or(-1),
               "deadline exhaustion should map to RecallTimeout");
}

}  // namespace

int main() {
  try {
    test_facade_computes_fixed_stage_budget_distribution();
    test_facade_fails_fast_when_deadline_is_exceeded_mid_pipeline();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
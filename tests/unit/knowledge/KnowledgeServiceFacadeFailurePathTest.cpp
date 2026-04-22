#include <exception>
#include <iostream>

#include "KnowledgeErrors.h"
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

[[nodiscard]] KnowledgeQuery make_query() {
  KnowledgeQuery query;
  query.request_id = "req-failure";
  query.query_text = "policy evidence";
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

void test_facade_rejects_retrieve_before_init() {
  KnowledgeServiceDeps deps;
  KnowledgeServiceFacade facade(std::move(deps));
  const auto result = facade.retrieve(make_query());
  assert_true(!result.ok,
              "retrieve before init should fail closed");
  assert_equal(static_cast<int>(KnowledgeErrorCode::NotInitialized),
               result.error->details.code.value_or(-1),
               "retrieve before init should map to NotInitialized");
}

void test_facade_rejects_disabled_knowledge() {
  KnowledgeConfigSnapshot config;
  config.knowledge_enabled = false;
  config.retrieval_mode_default = RetrievalMode::LexicalOnly;

  KnowledgeServiceFacade facade(KnowledgeServiceDeps{});
  assert_true(facade.init(config),
              "disabled config should still be a valid init input for the skeleton facade");

  const auto result = facade.retrieve(make_query());
  assert_true(!result.ok,
              "disabled knowledge should fail closed at retrieve entry");
  assert_equal(static_cast<int>(KnowledgeErrorCode::Disabled),
               result.error->details.code.value_or(-1),
               "disabled knowledge should map to Disabled");
}

void test_facade_propagates_normalizer_failure() {
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

  KnowledgeServiceDeps deps;
  deps.normalize_query = [](const KnowledgeQuery&) {
    dasall::knowledge::query::NormalizeResult result;
    result.ok = false;
    result.error = dasall::knowledge::make_knowledge_error_info(
        KnowledgeErrorCode::QueryValidationFailed,
        "query validation failed",
        "knowledge_service_facade.normalize_query",
        "normalize_failed");
    return result;
  };

  KnowledgeServiceFacade facade(std::move(deps));

  assert_true(facade.init(config),
              "facade should init before exercising normalize failure path");
  const auto result = facade.retrieve(make_query());
  assert_true(!result.ok,
              "normalizer failure should propagate as facade failure");
  assert_equal(static_cast<int>(KnowledgeErrorCode::QueryValidationFailed),
               result.error->details.code.value_or(-1),
               "normalizer failure should preserve query validation error code");
}

}  // namespace

int main() {
  try {
    test_facade_rejects_retrieve_before_init();
    test_facade_rejects_disabled_knowledge();
    test_facade_propagates_normalizer_failure();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
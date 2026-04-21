#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "support/TestAssertions.h"

#include "IKnowledgeService.h"
#include "KnowledgeErrors.h"
#include "KnowledgeTypes.h"

namespace {

using dasall::contracts::ResultCodeCategory;
using dasall::knowledge::AuthorityLevel;
using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::CorpusDescriptor;
using dasall::knowledge::EvidenceBundle;
using dasall::knowledge::EvidenceSlice;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::IKnowledgeService;
using dasall::knowledge::KnowledgeConfigSnapshot;
using dasall::knowledge::KnowledgeErrorCode;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::KnowledgeRetrieveResult;
using dasall::knowledge::RefreshResult;
using dasall::knowledge::RefreshStatus;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::SourceFormat;
using dasall::knowledge::SourceKind;
using dasall::knowledge::TrustLevel;
using dasall::knowledge::describe_knowledge_error;
using dasall::knowledge::make_knowledge_error_info;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

static_assert(std::is_abstract_v<IKnowledgeService>);
static_assert(std::is_polymorphic_v<IKnowledgeService>);
static_assert(std::is_same_v<std::underlying_type_t<KnowledgeErrorCode>, std::uint16_t>);
static_assert(std::is_same_v<decltype(&IKnowledgeService::init),
                             bool (IKnowledgeService::*)(const KnowledgeConfigSnapshot&)>);
static_assert(std::is_same_v<decltype(&IKnowledgeService::retrieve),
                             KnowledgeRetrieveResult (IKnowledgeService::*)(
                                 const KnowledgeQuery&)>);
static_assert(std::is_same_v<decltype(&IKnowledgeService::request_refresh),
                             RefreshResult (IKnowledgeService::*)(
                                 const CorpusChangeSet&)>);

void test_knowledge_query_surface_keeps_reserved_fields_and_defaults() {
  KnowledgeQuery query{
      .request_id = "req-knowledge-surface",
      .session_id = std::string("sess-01"),
      .goal_id = std::string("goal-01"),
      .query_text = "show me the recovery manager contract",
      .query_kind = KnowledgeQueryKind::MultiHop,
      .domain_tags = {"runtime", "contracts"},
      .allowed_corpora = {"adr_normative", "ssot_normative"},
      .latest_observation_digest_summary = std::string("digest"),
      .belief_state_summary = std::string("belief"),
      .top_k = 10U,
      .max_context_projection_items = 4U,
      .allow_stale = true,
      .retrieval_evidence_budget_hint = 256U,
  };

  assert_true(query.has_consistent_values(),
              "knowledge query surface should accept reserved fields and non-zero budgets");
  assert_true(query.query_kind == KnowledgeQueryKind::MultiHop,
              "knowledge query kind should expose the reserved MultiHop enum value");
}

void test_knowledge_surface_types_capture_runtime_facing_shapes() {
  EvidenceSlice slice{
      .evidence_id = "evidence-1",
      .snippet = "RecoveryManager owns recovery admission.",
      .citation_ref = "docs/adr/ADR-007#section-2",
      .confidence = 0.84F,
      .freshness = FreshnessState::Fresh,
      .tags = {"adr", "runtime"},
  };

  EvidenceBundle bundle{
      .slices = {slice},
      .context_projection = {"RecoveryManager owns recovery admission."},
      .omitted_sources = {},
      .degraded = false,
      .evidence_insufficient = false,
      .coverage_notes = "single slice",
  };

  CorpusDescriptor descriptor{
      .corpus_id = "adr_normative",
      .display_name = "ADR Normative",
      .source_uri = "docs/adr",
      .trust_level = TrustLevel::Trusted,
      .authority_level = AuthorityLevel::Normative,
      .source_kind = SourceKind::File,
      .allowed_formats = {SourceFormat::Markdown},
      .include_globs = {"*.md"},
      .exclude_globs = {"drafts/*"},
      .supported_modes = {RetrievalMode::LexicalOnly, RetrievalMode::Hybrid},
      .active_snapshot_id = "snapshot-01",
      .last_updated_ms = 42,
      .tags = {"adr", "normative"},
      .metadata = {{"baseline_class", "adr_normative"},
                   {"owner_module", "knowledge"},
                   {"refresh_strategy", "manual"},
                   {"default_language", "zh-CN"}},
  };

  KnowledgeConfigSnapshot config{
      .knowledge_enabled = true,
      .vector_enabled = true,
      .retrieval_mode_default = RetrievalMode::Hybrid,
      .evidence_budget_tokens = 512U,
      .max_context_projection_items = 6U,
      .catalog_refresh_interval_ms = 30000,
      .catalog_expire_after_ms = 60000,
      .allow_stale_read = true,
      .failure_backoff_ms = 5000,
      .request_deadline_ms = 900,
      .allow_budget_degrade = true,
      .max_parallel_recall = 2U,
      .sparse_recall_timeout_ms = 315,
      .dense_recall_timeout_ms = 315,
      .ingest_timeout_ms = 30000,
  };

  KnowledgeRetrieveResult success{
      .ok = true,
      .mode = RetrievalMode::Hybrid,
      .evidence = bundle,
      .error = std::nullopt,
  };

  CorpusChangeSet change_set{
      .added_sources = {"docs/adr/ADR-007.md"},
      .updated_sources = {"docs/ssot/CrossModuleDataProjectionMatrix.md"},
      .removed_sources = {},
  };

  RefreshResult refresh{
      .status = RefreshStatus::Accepted,
      .refresh_id = "refresh-knowledge-01",
      .error = std::nullopt,
  };

  assert_true(slice.has_consistent_values(),
              "evidence slice should keep snippet/citation/confidence semantics");
  assert_true(bundle.has_consistent_values(),
              "evidence bundle should keep projection count within slice count");
  assert_true(descriptor.has_consistent_values(),
              "corpus descriptor should require typed metadata and supported retrieval modes");
  assert_true(config.has_consistent_values(),
              "knowledge config snapshot should encode the projected runtime shape");
  assert_true(success.has_consistent_values(),
              "successful retrieve result should not carry ErrorInfo");
  assert_true(change_set.has_consistent_values(),
              "corpus change set should reject duplicate sources across each lane");
  assert_true(refresh.has_consistent_values(),
              "accepted refresh result should require a refresh id without an error");
}

void test_knowledge_error_projection_maps_all_failure_domains() {
  struct Expectation {
    KnowledgeErrorCode code;
    ResultCodeCategory category;
    std::string ref_type;
    bool retryable;
    bool safe_to_replan;
  };

  const std::vector<Expectation> expectations{
      {KnowledgeErrorCode::QueryValidationFailed, ResultCodeCategory::Validation,
       "knowledge::normalizer", false, false},
      {KnowledgeErrorCode::Disabled, ResultCodeCategory::Policy,
       "knowledge::config", false, true},
      {KnowledgeErrorCode::IndexStaleRejected, ResultCodeCategory::Policy,
       "knowledge::freshness", true, true},
      {KnowledgeErrorCode::EvidenceBudgetExhausted, ResultCodeCategory::Policy,
       "knowledge::assembler", false, true},
      {KnowledgeErrorCode::NoCorpusAvailable, ResultCodeCategory::Provider,
       "knowledge::router", false, true},
      {KnowledgeErrorCode::IndexUnavailable, ResultCodeCategory::Provider,
       "knowledge::index_reader", true, true},
      {KnowledgeErrorCode::VectorBackendUnavailable, ResultCodeCategory::Provider,
       "knowledge::vector_bridge", true, true},
      {KnowledgeErrorCode::RefreshFailed, ResultCodeCategory::Provider,
       "knowledge::index_writer", true, true},
      {KnowledgeErrorCode::NotInitialized, ResultCodeCategory::Runtime,
       "knowledge::facade", true, true},
      {KnowledgeErrorCode::RecallTimeout, ResultCodeCategory::Runtime,
       "knowledge::recall_coordinator", true, true},
      {KnowledgeErrorCode::RefreshBusy, ResultCodeCategory::Runtime,
       "knowledge::ingest_worker", true, true},
      {KnowledgeErrorCode::InternalError, ResultCodeCategory::Runtime,
       "knowledge::internal", false, true},
  };

  for (const auto& expectation : expectations) {
    const auto descriptor = describe_knowledge_error(expectation.code);
    const auto error = make_knowledge_error_info(expectation.code,
                                                 "mapped knowledge error",
                                                 "knowledge.surface.test");

    assert_true(error.failure_type.has_value() &&
                    *error.failure_type == expectation.category,
                "knowledge error mapping should project the expected failure category");
    assert_true(error.retryable.has_value() &&
                    *error.retryable == expectation.retryable,
                "knowledge error mapping should project retryable semantics");
    assert_true(error.safe_to_replan.has_value() &&
                    *error.safe_to_replan == expectation.safe_to_replan,
                "knowledge error mapping should project safe_to_replan semantics");
    assert_equal(static_cast<int>(expectation.code),
                 *error.details.code,
                 "knowledge error mapping should preserve the module-local error code");
    assert_equal(expectation.ref_type,
                 error.source_ref.ref_type,
                 "knowledge error mapping should anchor the source component");
    assert_equal(std::string(descriptor.default_ref_id),
                 error.source_ref.ref_id,
                 "knowledge error mapping should default the ref id to a stable code name");
  }
}

void test_knowledge_failure_result_requires_structured_error() {
  KnowledgeRetrieveResult failure{
      .ok = false,
      .mode = RetrievalMode::LexicalOnly,
      .evidence = std::nullopt,
      .error = make_knowledge_error_info(KnowledgeErrorCode::NotInitialized,
                                         "knowledge not initialized",
                                         "facade.retrieve"),
  };

  assert_true(failure.has_consistent_values(),
              "failed retrieve result should require a structured ErrorInfo payload");
}

}  // namespace

int main() {
  try {
    test_knowledge_query_surface_keeps_reserved_fields_and_defaults();
    test_knowledge_surface_types_capture_runtime_facing_shapes();
    test_knowledge_error_projection_maps_all_failure_domains();
    test_knowledge_failure_result_requires_structured_error();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
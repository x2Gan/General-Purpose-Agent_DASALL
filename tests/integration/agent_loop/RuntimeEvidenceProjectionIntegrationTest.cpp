#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for runtime evidence projection integration coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "IKnowledgeService.h"
#include "IResponseBuilder.h"
#include "KnowledgeTypes.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class StaticKnowledgeService final : public dasall::knowledge::IKnowledgeService {
 public:
  bool init(const dasall::knowledge::KnowledgeConfigSnapshot&) override { return true; }

  [[nodiscard]] dasall::knowledge::KnowledgeRetrieveResult retrieve(
      const dasall::knowledge::KnowledgeQuery& query) override {
    last_query = query;

    dasall::knowledge::KnowledgeRetrieveResult result;
    result.ok = true;
    result.mode = dasall::knowledge::RetrievalMode::LexicalOnly;
    result.evidence = dasall::knowledge::EvidenceBundle{
        .slices = {dasall::knowledge::EvidenceSlice{
            .evidence_id = "evidence-runtime-projection-001",
            .snippet = "Policy boundary owner contract keeps recovery ownership inside runtime.",
            .citation_ref = "ADR-0001#policy",
            .confidence = 0.91F,
            .freshness = dasall::knowledge::FreshnessState::Fresh,
            .tags = {"normative"},
        }},
        .context_projection = {
            "[normative] Policy boundary owner contract keeps recovery ownership inside runtime. (ADR-0001#policy)"},
        .omitted_sources = {},
        .degraded = false,
        .evidence_insufficient = false,
        .coverage_notes = "single normative policy hit",
    };
    result.retrieval_evidence_refs = {dasall::contracts::RetrievalEvidenceRef{
        .evidence_ref = "evidence-runtime-projection-001",
        .source_ref = "ADR-0001#policy",
        .source_kind = "file",
        .summary_text = "Policy boundary owner contract keeps recovery ownership inside runtime.",
        .trust_level = "trusted",
        .freshness = "fresh",
        .anchor_locator = std::string{"policy"},
    }};
    return result;
  }

  [[nodiscard]] dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const override {
    dasall::knowledge::KnowledgeHealthSnapshot snapshot;
    snapshot.state = dasall::knowledge::HealthState::Healthy;
    snapshot.active_snapshot_id = "snapshot-runtime-projection-001";
    snapshot.freshness_state = dasall::knowledge::FreshnessState::Fresh;
    snapshot.vector_backend_available = true;
    snapshot.last_known_good_available = true;
    snapshot.degraded_return_count = 0U;
    return snapshot;
  }

  [[nodiscard]] dasall::knowledge::RefreshResult request_refresh(
      const dasall::knowledge::CorpusChangeSet&) override {
    dasall::knowledge::RefreshResult result;
    result.status = dasall::knowledge::RefreshStatus::Busy;
    return result;
  }

  std::optional<dasall::knowledge::KnowledgeQuery> last_query;
};

class RecordingCognitionEngine final : public dasall::cognition::ICognitionEngine {
 public:
  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest& request) override {
    last_context_packet = request.context_packet;

    dasall::cognition::CognitionDecisionResult result;
    dasall::cognition::decision::ActionDecision decision;
    decision.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::ExecuteAction;
    decision.selected_node_id = std::string{"runtime-evidence-node"};
    decision.rationale =
        std::string{"runtime evidence projection gate emits executable action"};
    decision.confidence = 0.94F;
    decision.tool_intent_hint = dasall::cognition::decision::ToolIntentHint{
        .tool_name = std::string{"agent.dataset"},
        .intent_summary = std::string{"query the builtin dataset after evidence projection"},
        .argument_hints = {std::string{"policy boundary owner contract"}},
        .evidence_refs = {std::string{"evidence-runtime-projection-001"}},
    };
    decision.response_outline = dasall::cognition::decision::ResponseOutline{
        .summary = std::string{"runtime should preserve structured evidence refs"},
        .key_points = {std::string{"knowledge evidence refs reached cognition"}},
    };
    result.action_decision = std::move(decision);
    result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = true,
        .context_confidence = 0.95F,
        .missing_evidence_hints = {},
        .recommend_context_reload = false,
    };
    return result;
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest&) override {
    return {};
  }

  std::optional<dasall::contracts::ContextPacket> last_context_packet;
};

[[nodiscard]] std::shared_ptr<dasall::runtime::RuntimeDependencySet>
make_runtime_evidence_dependency_set(const dasall::memory::MemoryConfig& config,
                                     StaticKnowledgeService* knowledge_service,
                                     RecordingCognitionEngine* cognition_engine) {
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-runtime-evidence-019",
      "turn-runtime-evidence-019-001",
      "query policy boundary owner contract");
  dependency_set->cognition_engine =
      std::shared_ptr<dasall::cognition::ICognitionEngine>(cognition_engine);
  dependency_set->response_builder =
      std::shared_ptr<dasall::cognition::IResponseBuilder>(
          dasall::cognition::create_response_builder().release());
  dependency_set->knowledge_service =
      std::shared_ptr<dasall::knowledge::IKnowledgeService>(knowledge_service);
  dependency_set->external_evidence.push_back("runtime:evidence-projection-gate");
  return dependency_set;
}

void test_runtime_evidence_projection_integration_preserves_structured_refs() {
  const auto database_path =
      make_temp_database_path("dasall-runtime-evidence-projection-integration");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto* knowledge_service = new StaticKnowledgeService();
  auto* cognition_engine = new RecordingCognitionEngine();
  auto dependency_set =
      make_runtime_evidence_dependency_set(config, knowledge_service, cognition_engine);

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-runtime-evidence-019",
      "desktop_full",
      "runtime-evidence-projection-integration"));
  assert_true(init_result.accepted,
              "runtime evidence projection gate should initialize AgentFacade with live dependency ports");

  const auto result = facade.handle(make_agent_request(
      "req-runtime-evidence-019",
      "session-runtime-evidence-019",
      "trace-runtime-evidence-019",
      "query policy boundary owner contract"));

  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "runtime evidence projection gate should complete the unary request");
  assert_true(knowledge_service->last_query.has_value(),
              "runtime evidence projection gate should call knowledge_service->retrieve");
  assert_equal(std::string{"req-runtime-evidence-019"},
               knowledge_service->last_query->request_id,
               "runtime evidence projection gate should preserve request_id on knowledge query");
  assert_true(cognition_engine->last_context_packet.has_value(),
              "runtime evidence projection gate should expose a context packet to cognition");
  assert_true(cognition_engine->last_context_packet->retrieval_evidence_refs.has_value(),
              "runtime evidence projection gate should preserve structured evidence refs into ContextPacket");
  assert_equal(1,
               static_cast<int>(cognition_engine->last_context_packet
                                    ->retrieval_evidence_refs->size()),
               "runtime evidence projection gate should keep one structured evidence ref");
  assert_equal(std::string{"evidence-runtime-projection-001"},
               cognition_engine->last_context_packet->retrieval_evidence_refs->front().evidence_ref,
               "runtime evidence projection gate should preserve evidence_ref");
  assert_equal(std::string{"fresh"},
               cognition_engine->last_context_packet->retrieval_evidence_refs->front().freshness,
               "runtime evidence projection gate should preserve freshness");
  assert_true(cognition_engine->last_context_packet->retrieval_evidence.has_value() &&
                  std::any_of(
                      cognition_engine->last_context_packet->retrieval_evidence->begin(),
                      cognition_engine->last_context_packet->retrieval_evidence->end(),
                      [](const std::string& item) {
                        return item.find("ADR-0001#policy") != std::string::npos;
                      }),
              "runtime evidence projection gate should preserve citation-bearing text evidence into ContextPacket");

  dependency_set->memory_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_runtime_evidence_projection_integration_preserves_structured_refs();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
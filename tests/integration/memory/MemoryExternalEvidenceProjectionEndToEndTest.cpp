#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for memory external evidence projection coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "IKnowledgeService.h"
#include "IMemoryManager.h"
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
            .evidence_id = "evidence-memory-external-001",
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
        .evidence_ref = "evidence-memory-external-001",
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
    snapshot.active_snapshot_id = "snapshot-memory-external-001";
    snapshot.freshness_state = dasall::knowledge::FreshnessState::Fresh;
    snapshot.vector_backend_available = true;
    snapshot.last_known_good_available = true;
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

class RecordingMemoryManager final : public dasall::memory::IMemoryManager {
 public:
  explicit RecordingMemoryManager(std::shared_ptr<dasall::memory::IMemoryManager> delegate)
      : delegate_(std::move(delegate)) {}

  dasall::contracts::ResultCode init(const dasall::memory::MemoryConfig& config) override {
    return delegate_->init(config);
  }

  void shutdown() noexcept override { delegate_->shutdown(); }

  [[nodiscard]] dasall::memory::ContextAssemblyResult prepare_context(
      const dasall::memory::MemoryContextRequest& request) override {
    last_request = request;
    return delegate_->prepare_context(request);
  }

  [[nodiscard]] dasall::memory::WritebackResult write_back(
      const dasall::memory::MemoryWritebackRequest& request) override {
    return delegate_->write_back(request);
  }

  [[nodiscard]] dasall::memory::WorkingMemoryExportResult export_working_memory_snapshot(
      const dasall::memory::WorkingMemoryExportRequest& request) override {
    return delegate_->export_working_memory_snapshot(request);
  }

  [[nodiscard]] dasall::memory::MaintenanceReport run_maintenance(
      const dasall::memory::MaintenanceRequest& request) override {
    return delegate_->run_maintenance(request);
  }

  std::optional<dasall::memory::MemoryContextRequest> last_request;

 private:
  std::shared_ptr<dasall::memory::IMemoryManager> delegate_;
};

class DirectResponseCognitionEngine final : public dasall::cognition::ICognitionEngine {
 public:
  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest& request) override {
    last_context_packet = request.context_packet;

    dasall::cognition::CognitionDecisionResult result;
    dasall::cognition::decision::ActionDecision decision;
    decision.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::DirectResponse;
    decision.selected_node_id = std::string{"memory-external-evidence-node"};
    decision.rationale = std::string{"knowledge evidence projection reached memory prepare_context"};
    decision.confidence = 0.95F;
    decision.response_outline = dasall::cognition::decision::ResponseOutline{
        .summary = std::string{"memory external evidence projection completed"},
        .key_points = {std::string{"knowledge evidence reached memory"}},
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

void test_memory_external_evidence_projection_reaches_memory_prepare_context() {
  const auto database_path =
      make_temp_database_path("dasall-memory-external-evidence-projection");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-memory-external-007",
      "turn-memory-external-007-001",
      "query policy boundary owner contract");

  auto real_memory_manager = dependency_set->memory_manager;
  auto recording_memory_manager =
      std::make_shared<RecordingMemoryManager>(real_memory_manager);
  auto* knowledge_service = new StaticKnowledgeService();
  auto* cognition_engine = new DirectResponseCognitionEngine();

  dependency_set->memory_manager = recording_memory_manager;
  dependency_set->knowledge_service =
      std::shared_ptr<dasall::knowledge::IKnowledgeService>(knowledge_service);
  dependency_set->cognition_engine =
      std::shared_ptr<dasall::cognition::ICognitionEngine>(cognition_engine);
  dependency_set->response_builder =
      std::shared_ptr<dasall::cognition::IResponseBuilder>(
          dasall::cognition::create_response_builder().release());
  dependency_set->external_evidence = {"runtime:knowledge-evidence-integration"};

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-memory-external-007",
      "desktop_full",
      "memory-external-evidence-projection"));
  assert_true(init_result.accepted,
              "memory external evidence projection coverage should initialize AgentFacade with live dependency ports");

  const auto result = facade.handle(make_agent_request(
      "req-memory-external-007",
      "session-memory-external-007",
      "trace-memory-external-007",
      "query policy boundary owner contract"));

  (void)result;
  assert_true(knowledge_service->last_query.has_value(),
              "memory external evidence projection coverage should call knowledge_service->retrieve");
  assert_true(recording_memory_manager->last_request.has_value(),
              "memory external evidence projection coverage should pass a context request into memory prepare_context");
  assert_true(std::find(recording_memory_manager->last_request->external_evidence.begin(),
                        recording_memory_manager->last_request->external_evidence.end(),
                        std::string{"runtime:knowledge-evidence-integration"}) !=
                  recording_memory_manager->last_request->external_evidence.end(),
              "memory external evidence projection coverage should preserve runtime baseline evidence");
  assert_true(std::any_of(recording_memory_manager->last_request->external_evidence.begin(),
                          recording_memory_manager->last_request->external_evidence.end(),
                          [](const std::string& item) {
                            return item.find("Policy boundary owner contract") != std::string::npos;
                          }),
              "memory external evidence projection coverage should append knowledge text projection into MemoryContextRequest.external_evidence");
  assert_equal(1,
               static_cast<int>(recording_memory_manager->last_request->retrieval_evidence_refs.size()),
               "memory external evidence projection coverage should preserve the structured evidence ref alongside the text view");
  assert_equal(std::string{"evidence-memory-external-001"},
               recording_memory_manager->last_request->retrieval_evidence_refs.front().evidence_ref,
               "memory external evidence projection coverage should preserve evidence_ref on the memory request");
  assert_equal(std::string{"fresh"},
               recording_memory_manager->last_request->retrieval_evidence_refs.front().freshness,
               "memory external evidence projection coverage should preserve freshness on the memory request");
  assert_true(cognition_engine->last_context_packet.has_value(),
              "memory external evidence projection coverage should still build a context packet for cognition");

  recording_memory_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_memory_external_evidence_projection_reaches_memory_prepare_context();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
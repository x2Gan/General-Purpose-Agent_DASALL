#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition runtime interaction contract coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "CognitionTypes.h"
#include "ICognitionEngine.h"
#include "IMemoryManager.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

class ContractProbeMemoryManager final : public dasall::memory::IMemoryManager {
 public:
  explicit ContractProbeMemoryManager(bool fail_writeback = false)
      : fail_writeback_(fail_writeback) {}

  dasall::contracts::ResultCode init(const dasall::memory::MemoryConfig&) override {
    return static_cast<dasall::contracts::ResultCode>(0);
  }

  void shutdown() noexcept override {}

  [[nodiscard]] dasall::memory::ContextAssemblyResult prepare_context(
      const dasall::memory::MemoryContextRequest& request) override {
    ++prepare_context_calls;
    last_context_request = request;

    dasall::memory::ContextAssemblyResult result;
    result.context_packet.request_id = request.request_id;
    result.context_packet.user_turn = request.goal_summary;
    result.context_packet.current_goal_summary = request.goal_summary;
    result.context_packet.recent_history = std::vector<std::string>{};
    result.context_packet.latest_observation_digest_summary =
        request.latest_observation_digest_summary;
    result.context_packet.active_tools = request.visible_tools;
    return result;
  }

  [[nodiscard]] dasall::memory::WritebackResult write_back(
      const dasall::memory::MemoryWritebackRequest& request) override {
    ++write_back_calls;
    last_writeback_request = request;

    dasall::memory::WritebackResult result;
    if (fail_writeback_) {
      result.result_code = dasall::contracts::ResultCode::RuntimeRetryExhausted;
      result.retryable_storage_failure = true;
      return result;
    }

    result.persisted_turn_id = request.turn.turn_id;
    if (request.summary_candidate.has_value()) {
      result.summary_id = std::string{"summary-"} +
                          request.turn.turn_id.value_or(std::string{"turn"});
    }
    for (std::size_t index = 0; index < request.fact_candidates.size(); ++index) {
      result.fact_ids.push_back(
          request.turn.turn_id.value_or(std::string{"turn"}) + "-fact-" +
          std::to_string(index));
    }
    return result;
  }

  [[nodiscard]] dasall::memory::WorkingMemoryExportResult export_working_memory_snapshot(
      const dasall::memory::WorkingMemoryExportRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::MaintenanceReport run_maintenance(
      const dasall::memory::MaintenanceRequest&) override {
    return {};
  }

  int prepare_context_calls = 0;
  int write_back_calls = 0;
  std::optional<dasall::memory::MemoryContextRequest> last_context_request;
  std::optional<dasall::memory::MemoryWritebackRequest> last_writeback_request;

 private:
  bool fail_writeback_ = false;
};

class ContractProbeCognitionEngine final : public dasall::cognition::ICognitionEngine {
 public:
  explicit ContractProbeCognitionEngine(bool executable)
      : executable_(executable) {}

  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest&) override {
    dasall::cognition::CognitionDecisionResult result;

    if (executable_) {
      dasall::cognition::decision::ActionDecision decision;
      decision.decision_kind =
          dasall::cognition::decision::ActionDecisionKind::ExecuteAction;
      decision.selected_node_id = std::string{"contract-probe-node"};
      decision.rationale = std::string{"contract probe emits executable action"};
      decision.confidence = 0.91F;
      decision.tool_intent_hint = dasall::cognition::decision::ToolIntentHint{
          .tool_name = std::string{"agent.dataset"},
          .intent_summary = std::string{"probe runtime interaction contract"},
          .argument_hints = {std::string{"query=contract"}},
          .evidence_refs = {std::string{"tests:cognition-runtime-contract"}},
      };
      result.action_decision = std::move(decision);

      result.belief_update_hint = dasall::cognition::belief::BeliefUpdateHint{
          .confirmed_facts_delta = {
              dasall::cognition::belief::FactDelta{
                  .fact = std::string{"runtime consumed executable cognition decision"},
              },
          },
          .hypotheses_delta = {},
          .assumptions_delta = {},
          .evidence_refs_delta = {},
          .missing_evidence_refs = {},
          .confidence_hint = 0.8F,
          .merge_mode = dasall::cognition::belief::BeliefMergeMode::Merge,
      };
      result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
          .context_sufficient = true,
          .context_confidence = 0.8F,
          .missing_evidence_hints = {},
          .recommend_context_reload = false,
      };
      return result;
    }

    dasall::cognition::decision::ActionDecision non_executable;
    non_executable.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::AskClarification;
    non_executable.rationale = std::string{"contract probe asks clarification"};
    non_executable.confidence = 0.42F;
    non_executable.clarification_needed = true;
    non_executable.clarification_question =
        std::string{"missing evidence for executable action"};
    result.action_decision = std::move(non_executable);

    result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = false,
        .context_confidence = 0.3F,
        .missing_evidence_hints = {std::string{"belief_state.confirmed_facts"}},
        .recommend_context_reload = true,
    };
    return result;
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest&) override {
    dasall::cognition::CognitionReflectionResult result;
    return result;
  }

 private:
  bool executable_;
};

void test_action_decision_execute_action_maps_to_runtime_progress() {
  const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-ok");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config, "session-027-ok", "turn-027-ok-001", "query interaction contract success");
    auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
    dependency_set->memory_manager = contract_memory_manager;
  dependency_set->cognition_engine =
      std::make_shared<ContractProbeCognitionEngine>(true);

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set, "rt-027-ok", "desktop_full", "cognition-runtime-contract-ok"));
  assert_true(init_result.accepted,
              "interaction contract success case should initialize AgentFacade");

  const auto result = facade.handle(make_agent_request(
      "req-027-ok", "session-027-ok", "trace-027-ok", "query interaction contract success"));

  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "execute action should allow runtime FSM to progress to completed");
  assert_true(result.task_completed.value_or(false),
              "execute action should keep task_completed=true");
  assert_true(contract_memory_manager->write_back_calls == 1,
              "execute action should project the cognition belief hint through memory writeback exactly once");
  assert_true(contract_memory_manager->last_writeback_request.has_value() &&
                  contract_memory_manager->last_writeback_request->summary_candidate.has_value() &&
                  !contract_memory_manager->last_writeback_request->fact_candidates.empty(),
              "execute action should materialize a summary and facts for the cognition belief hint");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

void test_non_executable_decision_is_rejected_by_runtime_contract() {
  const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-reject");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-027-reject",
      "turn-027-reject-001",
      "query interaction contract rejection");
    auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
    dependency_set->memory_manager = contract_memory_manager;
  dependency_set->cognition_engine =
      std::make_shared<ContractProbeCognitionEngine>(false);

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-027-reject",
      "desktop_full",
      "cognition-runtime-contract-reject"));
  assert_true(init_result.accepted,
              "interaction contract rejection case should initialize AgentFacade");

  const auto result = facade.handle(make_agent_request(
      "req-027-reject",
      "session-027-reject",
      "trace-027-reject",
      "query interaction contract rejection"));

  assert_true(result.status == dasall::contracts::AgentResultStatus::PartiallyCompleted,
              "non-executable decision with a context reload recommendation should degrade to waiting clarify");
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("missing evidence for executable action") !=
                      std::string::npos,
              "runtime should surface the cognition clarification question after the bounded refresh attempt");
  assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
              "clarification degrade should keep the turn resumable through a waiting checkpoint");
  assert_true(contract_memory_manager->prepare_context_calls == 2,
              "recommend_context_reload should trigger exactly one additional context refresh before clarification degrade");
  assert_true(contract_memory_manager->write_back_calls == 0,
              "non-executable clarification path without a belief hint should not emit writeback traffic");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

void test_belief_writeback_failure_does_not_override_completed_result() {
  const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-writeback-fail");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-027-writeback-fail",
      "turn-027-writeback-fail-001",
      "query interaction contract writeback failure");
  auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>(true);
  dependency_set->memory_manager = contract_memory_manager;
  dependency_set->cognition_engine =
      std::make_shared<ContractProbeCognitionEngine>(true);

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-027-writeback-fail",
      "desktop_full",
      "cognition-runtime-contract-writeback-fail"));
  assert_true(init_result.accepted,
              "writeback failure case should initialize AgentFacade");

  const auto result = facade.handle(make_agent_request(
      "req-027-writeback-fail",
      "session-027-writeback-fail",
      "trace-027-writeback-fail",
      "query interaction contract writeback failure"));

  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "best-effort belief writeback failure should not override the completed runtime result");
  assert_true(contract_memory_manager->write_back_calls == 1,
              "writeback failure case should still attempt the cognition belief projection once");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_action_decision_execute_action_maps_to_runtime_progress();
    test_non_executable_decision_is_rejected_by_runtime_contract();
    test_belief_writeback_failure_does_not_override_completed_result();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

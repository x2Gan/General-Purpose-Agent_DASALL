#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition runtime interaction contract coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "CognitionTypes.h"
#include "ICognitionEngine.h"
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

  assert_true(result.status == dasall::contracts::AgentResultStatus::Failed,
              "non-executable decision should be rejected by runtime main loop contract");
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("requires cognition to select an executable action") !=
                      std::string::npos,
              "runtime should surface explicit contract rejection message for non-executable action");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.stage == "main_loop",
              "runtime should map interaction contract failure back to main_loop stage");

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
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition runtime interaction contract coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "CognitionTypes.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "IMemoryManager.h"
#include "IToolManager.h"
#include "MockCognitionFixture.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::runtime_fixture::make_true_integration_policy_snapshot;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

enum class ContractDecisionMode : std::uint8_t {
  ExecuteAction = 0,
  DirectResponse,
  ConvergeSafe,
  AskClarification,
  NoDecision,
};

enum class ContractReflectionMode : std::uint8_t {
  None = 0,
  Continue,
  RetryStep,
  Replan,
  AbortSafe,
};

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
  ContractProbeCognitionEngine(
      ContractDecisionMode decision_mode,
      ContractReflectionMode reflection_mode = ContractReflectionMode::None)
      : decision_mode_(decision_mode),
        reflection_mode_(reflection_mode) {}

  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest&) override {
    dasall::cognition::CognitionDecisionResult result;

    if (decision_mode_ == ContractDecisionMode::ExecuteAction) {
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

      if (decision_mode_ == ContractDecisionMode::DirectResponse ||
        decision_mode_ == ContractDecisionMode::ConvergeSafe) {
        dasall::cognition::decision::ActionDecision terminal_decision;
        terminal_decision.decision_kind =
          decision_mode_ == ContractDecisionMode::DirectResponse
            ? dasall::cognition::decision::ActionDecisionKind::DirectResponse
            : dasall::cognition::decision::ActionDecisionKind::ConvergeSafe;
        terminal_decision.rationale =
          decision_mode_ == ContractDecisionMode::DirectResponse
            ? std::string{"contract probe emits a direct terminal response"}
            : std::string{"contract probe emits a safe convergence response"};
        terminal_decision.confidence = 0.83F;
        terminal_decision.response_outline = dasall::cognition::decision::ResponseOutline{
          .summary = decision_mode_ == ContractDecisionMode::DirectResponse
                 ? std::string{"direct response contract summary"}
                 : std::string{"safe convergence contract summary"},
          .key_points = {std::string{"runtime responding mapped"},
                 std::string{"response builder invoked"}},
        };
        result.action_decision = std::move(terminal_decision);
        result.belief_update_hint = dasall::cognition::belief::BeliefUpdateHint{
          .confirmed_facts_delta = {
            dasall::cognition::belief::FactDelta{
              .fact = std::string{"runtime consumed terminal cognition decision"},
            },
          },
          .hypotheses_delta = {},
          .assumptions_delta = {},
          .evidence_refs_delta = {},
          .missing_evidence_refs = {},
          .confidence_hint = 0.75F,
          .merge_mode = dasall::cognition::belief::BeliefMergeMode::Merge,
        };
        result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
          .context_sufficient = true,
          .context_confidence = 0.76F,
          .missing_evidence_hints = {},
          .recommend_context_reload = false,
        };
        return result;
      }

      if (decision_mode_ == ContractDecisionMode::AskClarification) {
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

      dasall::cognition::decision::ActionDecision no_decision;
      no_decision.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::NoDecision;
      no_decision.rationale = std::string{"contract probe produced no executable or terminal decision"};
      no_decision.confidence = 0.12F;
      result.action_decision = std::move(no_decision);
      result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = true,
        .context_confidence = 0.60F,
        .missing_evidence_hints = {},
        .recommend_context_reload = false,
      };
    return result;
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest& request) override {
    dasall::cognition::CognitionReflectionResult result;
    if (reflection_mode_ == ContractReflectionMode::None) {
      return result;
    }

    dasall::contracts::ReflectionDecision reflection_decision;
    reflection_decision.request_id = request.request_id;
    reflection_decision.goal_id = request.goal_contract.goal_id;
    reflection_decision.rationale = std::string{"contract probe reflection requested runtime recovery"};
    reflection_decision.confidence = 0.67F;
    reflection_decision.relevant_observation_refs =
        std::vector<std::string>{request.latest_observation.observation_id.value_or(
            std::string{"obs-contract-reflection"})};
    reflection_decision.hint_ref = std::string{"tests:cognition-runtime-reflection-contract"};
    reflection_decision.created_at = 1700000270;

    switch (reflection_mode_) {
      case ContractReflectionMode::Continue:
        reflection_decision.decision_kind = dasall::contracts::ReflectionDecisionKind::Continue;
        reflection_decision.rationale = std::string{"reflection requested continue resume"};
        break;
      case ContractReflectionMode::RetryStep:
        reflection_decision.decision_kind = dasall::contracts::ReflectionDecisionKind::RetryStep;
        reflection_decision.rationale = std::string{"reflection requested retry of the waiting tool step"};
        break;
      case ContractReflectionMode::Replan:
        reflection_decision.decision_kind = dasall::contracts::ReflectionDecisionKind::Replan;
        reflection_decision.rationale = std::string{"reflection requested replan before response"};
        break;
      case ContractReflectionMode::AbortSafe:
        reflection_decision.decision_kind = dasall::contracts::ReflectionDecisionKind::AbortSafe;
        reflection_decision.rationale = std::string{"reflection requested abort_safe"};
        break;
      case ContractReflectionMode::None:
        break;
    }

    result.reflection_decision = std::move(reflection_decision);
    return result;
  }

 private:
  ContractDecisionMode decision_mode_;
  ContractReflectionMode reflection_mode_;
};

class ContractProbeResponseBuilder final : public dasall::cognition::IResponseBuilder {
 public:
  [[nodiscard]] dasall::cognition::ResponseBuildResult build(
      const dasall::cognition::ResponseBuildRequest& request) override {
    ++build_calls;
    last_request = request;

    dasall::contracts::AgentResult agent_result;
    agent_result.result_id = std::string{"agent-result-contract-"} + request.request_id;
    agent_result.status = dasall::contracts::AgentResultStatus::Completed;
    agent_result.result_code = 0;
    const auto response_summary =
      request.terminal_decision->response_outline.has_value()
        ? request.terminal_decision->response_outline->summary
        : request.latest_observation.has_value() && request.latest_observation->payload.has_value()
            ? *request.latest_observation->payload
            : std::string{"contract probe response"};
    agent_result.response_text = std::string{"contract probe response: "} + response_summary;
    agent_result.task_completed = true;
    agent_result.created_at = 1700000400;
    agent_result.request_id = request.request_id;
    agent_result.trace_id = request.trace_id;
    agent_result.goal_id = request.goal_contract.goal_id;

    dasall::cognition::ResponseBuildResult result;
    result.agent_result = std::move(agent_result);
    result.diagnostics.push_back("contract_probe_response_builder");
    return result;
  }

  int build_calls = 0;
  std::optional<dasall::cognition::ResponseBuildRequest> last_request;
};

class StructuredDecisionDelegatingCognitionEngine final
    : public dasall::cognition::ICognitionEngine {
 public:
  explicit StructuredDecisionDelegatingCognitionEngine(
      std::unique_ptr<dasall::cognition::ICognitionEngine> delegate)
      : delegate_(std::move(delegate)) {}

  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest& request) override {
    ++decide_calls;
    last_decide_request = request;
    return delegate_->decide(request);
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest& request) override {
    ++reflect_calls;
    last_reflection_request = request;
    return {};
  }

  int decide_calls = 0;
  int reflect_calls = 0;
  std::optional<dasall::cognition::CognitionStepRequest> last_decide_request;
  std::optional<dasall::cognition::ReflectionRequest> last_reflection_request;

 private:
  std::unique_ptr<dasall::cognition::ICognitionEngine> delegate_;
};

class ContractProbeToolManager final : public dasall::tools::IToolManager {
 public:
  [[nodiscard]] dasall::tools::ToolInvocationEnvelope invoke(
      const dasall::contracts::ToolRequest& request,
      const dasall::tools::ToolInvocationContext& context) override {
    ++invoke_calls;
    last_request = request;
    last_context = context;

    const auto observation_id = std::string{"obs-"} +
                                request.tool_call_id.value_or(std::string{"tool-call-missing"});

    dasall::contracts::ToolResult tool_result;
    tool_result.request_id = request.request_id;
    tool_result.tool_call_id = request.tool_call_id;
    tool_result.tool_name = request.tool_name;
    tool_result.success = true;
    tool_result.payload = response_payload;
    tool_result.completed_at = 1700000310;
    tool_result.duration_ms = 12;
    tool_result.goal_id = request.goal_id;
    tool_result.worker_task_id = request.worker_task_id;
    tool_result.tags = std::vector<std::string>{"tests", "runtime-contract", "structured"};

    dasall::contracts::Observation observation;
    observation.observation_id = observation_id;
    observation.source = dasall::contracts::ObservationSource::ToolExecution;
    observation.success = true;
    observation.payload = response_payload;
    observation.created_at = 1700000311;
    observation.tool_call_id = request.tool_call_id;
    observation.request_id = request.request_id;
    observation.goal_id = request.goal_id;
    observation.duration_ms = 12;
    observation.tags = std::vector<std::string>{"tests", "runtime-contract", "structured"};

    dasall::contracts::ObservationDigest observation_digest;
    observation_digest.observation_id = observation_id;
    observation_digest.summary = response_summary;
    observation_digest.key_facts = std::vector<std::string>{
        "structured execute action reached the runtime tool handoff", response_payload};
    observation_digest.citations = std::vector<std::string>{
        std::string{"tool_call:"} +
        request.tool_call_id.value_or(std::string{"tool-call-missing"})};
    observation_digest.confidence = 0.88F;
    observation_digest.omitted_details = std::vector<std::string>{};
    observation_digest.source = dasall::contracts::ObservationSource::ToolExecution;
    observation_digest.created_at = 1700000312;
    observation_digest.tags = std::vector<std::string>{"tests", "runtime-contract", "structured"};

    dasall::tools::ToolInvocationEnvelope envelope;
    envelope.tool_result = std::move(tool_result);
    envelope.observation = std::move(observation);
    envelope.observation_digest = std::move(observation_digest);
    return envelope;
  }

  [[nodiscard]] std::vector<dasall::tools::ToolInvocationEnvelope> invoke_batch(
      std::span<const dasall::contracts::ToolRequest> requests,
      const dasall::tools::ToolInvocationContext& context) override {
    std::vector<dasall::tools::ToolInvocationEnvelope> envelopes;
    envelopes.reserve(requests.size());
    for (const auto& request : requests) {
      envelopes.push_back(invoke(request, context));
    }
    return envelopes;
  }

  [[nodiscard]] dasall::tools::ToolInvocationEnvelope compensate(
      const dasall::tools::CompensationRequest&,
      const dasall::tools::ToolInvocationContext&) override {
    return {};
  }

  int invoke_calls = 0;
  std::optional<dasall::contracts::ToolRequest> last_request;
  std::optional<dasall::tools::ToolInvocationContext> last_context;
  std::string response_payload = R"({"status":"ok","source":"structured-contract-probe"})";
  std::string response_summary = "structured execute action observation";
};

[[nodiscard]] std::shared_ptr<StructuredDecisionDelegatingCognitionEngine>
make_structured_contract_engine(const std::string& profile_id,
                                const MockCognitionFixture& fixture) {
  const auto snapshot = make_true_integration_policy_snapshot(profile_id);
  auto delegate = dasall::cognition::create_cognition_engine(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
          .llm_manager = fixture.llm_manager(),
      });
  assert_true(delegate != nullptr,
              "structured runtime interaction contract requires a snapshot-backed cognition engine");
  return std::make_shared<StructuredDecisionDelegatingCognitionEngine>(std::move(delegate));
}

void test_action_decision_execute_action_maps_to_runtime_progress() {
  const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-ok");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config, "session-027-ok", "turn-027-ok-001", "query interaction contract success");
    auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
    dependency_set->memory_manager = contract_memory_manager;
  dependency_set->cognition_engine =
        std::make_shared<ContractProbeCognitionEngine>(ContractDecisionMode::ExecuteAction);

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
        std::make_shared<ContractProbeCognitionEngine>(ContractDecisionMode::AskClarification);

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
      std::make_shared<ContractProbeCognitionEngine>(ContractDecisionMode::ExecuteAction);

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

  void test_reflection_continue_retry_and_replan_reenter_runtime_paths() {
    const struct ReflectionCase {
    ContractReflectionMode mode;
    std::string case_id;
    } cases[] = {
      {ContractReflectionMode::Continue, "reflection_continue"},
      {ContractReflectionMode::RetryStep, "reflection_retry"},
      {ContractReflectionMode::Replan, "reflection_replan"},
    };

    for (const auto& test_case : cases) {
    const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-" +
                               test_case.case_id);
    cleanup_database_artifacts(database_path);

    const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
    auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-027-" + test_case.case_id,
      "turn-027-" + test_case.case_id + "-001",
      "query interaction contract " + test_case.case_id);
    dependency_set->memory_manager = std::make_shared<ContractProbeMemoryManager>();
    dependency_set->cognition_engine = std::make_shared<ContractProbeCognitionEngine>(
      ContractDecisionMode::ExecuteAction,
      test_case.mode);

    dasall::runtime::AgentFacade facade;
    const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-027-" + test_case.case_id,
      "desktop_full",
      "cognition-runtime-contract-" + test_case.case_id));
    assert_true(init_result.accepted,
          "reflection contract case should initialize AgentFacade");

    const auto result = facade.handle(make_agent_request(
      "req-027-" + test_case.case_id,
      "session-027-" + test_case.case_id,
      "trace-027-" + test_case.case_id,
      "query interaction contract " + test_case.case_id));

    const auto status_value = result.status.has_value()
                    ? std::to_string(static_cast<int>(*result.status))
                    : std::string{"none"};
    const auto result_detail = result.error_info.has_value()
                     ? result.error_info->details.stage + ":" +
                       result.error_info->details.message
                     : result.response_text.value_or(std::string{"no-response"});

    assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
          std::string("reflection case should re-enter a completed runtime path: ") +
            test_case.case_id +
            " status=" +
            status_value +
            " detail=" + result_detail);
    assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
          std::string("reflection case should persist a terminal checkpoint: ") +
            test_case.case_id);

    if (dependency_set->memory_manager != nullptr) {
      dependency_set->memory_manager->shutdown();
    }
    cleanup_database_artifacts(database_path);
    }
  }

  void test_reflection_abort_safe_stops_mainline() {
    const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-reflection-abort");
    cleanup_database_artifacts(database_path);

    const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
    auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-027-reflection-abort",
      "turn-027-reflection-abort-001",
      "query interaction contract reflection abort");
    dependency_set->memory_manager = std::make_shared<ContractProbeMemoryManager>();
    dependency_set->cognition_engine = std::make_shared<ContractProbeCognitionEngine>(
      ContractDecisionMode::ExecuteAction,
      ContractReflectionMode::AbortSafe);

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-027-reflection-abort",
      "desktop_full",
      "cognition-runtime-contract-reflection-abort"));
  assert_true(init_result.accepted,
          "reflection abort case should initialize AgentFacade");

  const auto result = facade.handle(make_agent_request(
      "req-027-reflection-abort",
      "session-027-reflection-abort",
      "trace-027-reflection-abort",
      "query interaction contract reflection abort"));

    assert_true(result.status == dasall::contracts::AgentResultStatus::Failed,
          "abort_safe reflection should stop the mainline and return a failed safe-path result");
    assert_true(result.error_info.has_value() &&
            result.error_info->details.stage == "recovery_round",
          "abort_safe reflection should surface recovery_round diagnostics");
    assert_true(result.response_text.has_value() &&
            result.response_text->find("fail-safe") != std::string::npos,
          "abort_safe reflection should emit the fail-safe response text");
    assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
          "abort_safe reflection should still persist a terminal checkpoint");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

  void test_terminal_decisions_map_to_runtime_responding() {
    const struct TerminalDecisionCase {
    ContractDecisionMode mode;
    std::string case_id;
    dasall::cognition::decision::ActionDecisionKind expected_kind;
    std::string expected_summary;
    } cases[] = {
      {ContractDecisionMode::DirectResponse,
       "direct_response",
       dasall::cognition::decision::ActionDecisionKind::DirectResponse,
       "direct response contract summary"},
      {ContractDecisionMode::ConvergeSafe,
       "converge_safe",
       dasall::cognition::decision::ActionDecisionKind::ConvergeSafe,
       "safe convergence contract summary"},
    };

    for (const auto& test_case : cases) {
    const auto database_path = make_temp_database_path(
      "dasall-cognition-runtime-contract-" + test_case.case_id);
    cleanup_database_artifacts(database_path);

    const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
    auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-027-" + test_case.case_id,
      "turn-027-" + test_case.case_id + "-001",
      "query interaction contract " + test_case.case_id);
    auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
    auto contract_response_builder = std::make_shared<ContractProbeResponseBuilder>();
    dependency_set->memory_manager = contract_memory_manager;
    dependency_set->response_builder = contract_response_builder;
    dependency_set->cognition_engine =
      std::make_shared<ContractProbeCognitionEngine>(test_case.mode);

    dasall::runtime::AgentFacade facade;
    const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-027-" + test_case.case_id,
      "desktop_full",
      "cognition-runtime-contract-" + test_case.case_id));
    assert_true(init_result.accepted,
          "terminal decision contract case should initialize AgentFacade");

    const auto result = facade.handle(make_agent_request(
      "req-027-" + test_case.case_id,
      "session-027-" + test_case.case_id,
      "trace-027-" + test_case.case_id,
      "query interaction contract " + test_case.case_id));

    assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
          std::string{"terminal cognition decision should complete runtime: "} +
            test_case.case_id);
    assert_true(result.task_completed.value_or(false),
          std::string{"terminal cognition decision should keep task_completed=true: "} +
            test_case.case_id);
    assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
          std::string{"terminal cognition decision should persist a terminal checkpoint: "} +
            test_case.case_id);
    assert_true(contract_response_builder->build_calls == 1,
          std::string{"terminal cognition decision should call response builder exactly once: "} +
            test_case.case_id);
    assert_true(contract_response_builder->last_request.has_value() &&
            contract_response_builder->last_request->terminal_decision.has_value(),
          std::string{"terminal cognition decision should reach response builder with a terminal_decision: "} +
            test_case.case_id);
    assert_true(!contract_response_builder->last_request->latest_observation.has_value(),
          std::string{"terminal cognition decision should build without a tool observation: "} +
            test_case.case_id);
    assert_true(contract_response_builder->last_request->terminal_decision->decision_kind ==
            test_case.expected_kind,
          std::string{"runtime should preserve the terminal decision kind for response builder: "} +
            test_case.case_id);
    assert_true(result.response_text.has_value() &&
            result.response_text->find(test_case.expected_summary) != std::string::npos,
          std::string{"response builder output should surface the terminal summary: "} +
            test_case.case_id);
    assert_true(contract_memory_manager->write_back_calls == 1,
          std::string{"terminal cognition decision should still project belief writeback once: "} +
            test_case.case_id);

    if (dependency_set->memory_manager != nullptr) {
      dependency_set->memory_manager->shutdown();
    }
    cleanup_database_artifacts(database_path);
    }
  }

  void test_no_decision_fails_fast_before_response_builder() {
    const auto database_path = make_temp_database_path("dasall-cognition-runtime-contract-no-decision");
    cleanup_database_artifacts(database_path);

    const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
    auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-027-no-decision",
      "turn-027-no-decision-001",
      "query interaction contract no decision");
    auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
    auto contract_response_builder = std::make_shared<ContractProbeResponseBuilder>();
    dependency_set->memory_manager = contract_memory_manager;
    dependency_set->response_builder = contract_response_builder;
    dependency_set->cognition_engine =
      std::make_shared<ContractProbeCognitionEngine>(ContractDecisionMode::NoDecision);

    dasall::runtime::AgentFacade facade;
    const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-027-no-decision",
      "desktop_full",
      "cognition-runtime-contract-no-decision"));
    assert_true(init_result.accepted,
          "no-decision contract case should initialize AgentFacade");

    const auto result = facade.handle(make_agent_request(
      "req-027-no-decision",
      "session-027-no-decision",
      "trace-027-no-decision",
      "query interaction contract no decision"));

    assert_true(result.status == dasall::contracts::AgentResultStatus::Failed,
          "no-decision should fail fast on the live runtime contract path");
    assert_true(contract_response_builder->build_calls == 0,
          "no-decision should fail before response builder is invoked");
    assert_true(result.error_info.has_value() &&
            result.error_info->details.stage == "main_loop",
          "no-decision should surface main_loop diagnostics");
    assert_true(contract_memory_manager->write_back_calls == 0,
          "no-decision should not emit belief writeback traffic");

    if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
    }
    cleanup_database_artifacts(database_path);
  }

    void test_structured_execute_action_is_consumed_by_runtime_tool_handoff() {
      const auto database_path = make_temp_database_path(
        "dasall-cognition-runtime-contract-structured-execute");
      cleanup_database_artifacts(database_path);

      const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
      auto dependency_set = make_true_integration_dependency_set(
        config,
        "session-027-structured-execute",
        "turn-027-structured-execute-001",
        "query structured interaction contract execute action");
      auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
      auto contract_response_builder = std::make_shared<ContractProbeResponseBuilder>();
      auto contract_tool_manager = std::make_shared<ContractProbeToolManager>();
      MockCognitionFixture fixture(MockCognitionFixtureOptions{
        .selected_node_id = "structured-contract-node",
        .tool_name = "agent.dataset",
        .response_text = "structured execute action should be post-processed by runtime",
      });
      fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
      fixture.stage_structured_execution_result(StructuredExecutionPayloadScenario::ValidExecuteAction);

      dependency_set->memory_manager = contract_memory_manager;
      dependency_set->llm_manager = fixture.llm_manager();
      dependency_set->response_builder = contract_response_builder;
      dependency_set->tool_manager = contract_tool_manager;
      auto structured_engine = make_structured_contract_engine("desktop_full", fixture);
      dependency_set->cognition_engine = structured_engine;

      dasall::runtime::AgentFacade facade;
      auto init_request = make_true_integration_init_request(
        dependency_set,
        "rt-027-structured-execute",
        "desktop_full",
        "cognition-runtime-contract-structured-execute");
      init_request.policy_snapshot = make_true_integration_policy_snapshot("desktop_full");
      const auto init_result = facade.init(init_request);
      assert_true(init_result.accepted,
            "structured execute action contract case should initialize AgentFacade");

      const auto result = facade.handle(make_agent_request(
        "req-027-structured-execute",
        "session-027-structured-execute",
        "trace-027-structured-execute",
        "query structured interaction contract execute action"));

      assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
            "projected execute action should let runtime complete through the tool round");
      assert_true(result.task_completed.value_or(false),
            "structured execute action should keep task_completed=true");
      assert_true(structured_engine->decide_calls == 1,
            "runtime should consume the projected ActionDecision through the real decide path");
      assert_true(structured_engine->reflect_calls == 1,
            "execute action path should re-enter reflection exactly once after the tool hop");
      assert_true(contract_tool_manager->invoke_calls == 1,
            "projected execute action should hand off exactly one ToolRequest to runtime tools");
      assert_true(contract_tool_manager->last_request.has_value(),
            "tool handoff probe should retain the last ToolRequest");
      assert_equal(std::string{"agent.dataset"},
             contract_tool_manager->last_request->tool_name.value_or(std::string{}),
             "runtime should map projected tool_intent_hint.tool_name into ToolRequest.tool_name");
      assert_equal(std::string{"{\"query\":\"query=current_state\"}"},
             contract_tool_manager->last_request->arguments_payload.value_or(std::string{}),
             "runtime should map projected tool_intent_hint.argument_hints into ToolRequest.arguments_payload");
      assert_true(contract_response_builder->build_calls == 1,
            "execute action path should materialize one response build after tool projection");
      assert_true(contract_response_builder->last_request.has_value() &&
              contract_response_builder->last_request->terminal_decision.has_value(),
            "execute action path should reach response builder with the projected ActionDecision");
      assert_true(contract_response_builder->last_request->terminal_decision->decision_kind ==
              dasall::cognition::decision::ActionDecisionKind::ExecuteAction,
            "runtime response build should keep the projected execute_action decision kind");
      assert_equal(std::string{"structured-contract-node"},
             contract_response_builder->last_request->terminal_decision->selected_node_id.value_or(
               std::string{}),
             "runtime response build should preserve the projected selected_node_id");
      assert_true(contract_response_builder->last_request->latest_observation.has_value() &&
              contract_response_builder->last_request->latest_observation->payload.has_value(),
            "execute action path should pass the projected latest observation into response builder");
      assert_true(contract_response_builder->last_request->latest_observation->payload->find(
              "structured-contract-probe") != std::string::npos,
            "response builder should observe the tool projection produced by the runtime first hop");

      if (dependency_set->memory_manager != nullptr) {
      dependency_set->memory_manager->shutdown();
      }
      cleanup_database_artifacts(database_path);
    }

    void test_structured_terminal_decisions_are_consumed_by_runtime_response_builder() {
      const struct StructuredTerminalCase {
      StructuredExecutionPayloadScenario scenario;
      std::string case_id;
      dasall::cognition::decision::ActionDecisionKind expected_kind;
      std::string expected_summary;
      } cases[] = {
        {StructuredExecutionPayloadScenario::ValidDirectResponse,
         "structured-direct-response",
         dasall::cognition::decision::ActionDecisionKind::DirectResponse,
         "structured direct response contract summary"},
        {StructuredExecutionPayloadScenario::ValidConvergeSafe,
         "structured-converge-safe",
         dasall::cognition::decision::ActionDecisionKind::ConvergeSafe,
         "structured converge safe contract summary"},
      };

      for (const auto& test_case : cases) {
      const auto database_path = make_temp_database_path(
        "dasall-cognition-runtime-contract-" + test_case.case_id);
      cleanup_database_artifacts(database_path);

      const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
      auto dependency_set = make_true_integration_dependency_set(
        config,
        "session-027-" + test_case.case_id,
        "turn-027-" + test_case.case_id + "-001",
        "query " + test_case.case_id);
      auto contract_memory_manager = std::make_shared<ContractProbeMemoryManager>();
      auto contract_response_builder = std::make_shared<ContractProbeResponseBuilder>();
      auto contract_tool_manager = std::make_shared<ContractProbeToolManager>();
      MockCognitionFixture fixture(MockCognitionFixtureOptions{
        .selected_node_id = "structured-terminal-node",
        .response_text = test_case.expected_summary,
      });
      fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
      fixture.stage_structured_execution_result(test_case.scenario);

      dependency_set->memory_manager = contract_memory_manager;
      dependency_set->llm_manager = fixture.llm_manager();
      dependency_set->response_builder = contract_response_builder;
      dependency_set->tool_manager = contract_tool_manager;
      auto structured_engine = make_structured_contract_engine("desktop_full", fixture);
      dependency_set->cognition_engine = structured_engine;

      dasall::runtime::AgentFacade facade;
      auto init_request = make_true_integration_init_request(
        dependency_set,
        "rt-027-" + test_case.case_id,
        "desktop_full",
        "cognition-runtime-contract-" + test_case.case_id);
      init_request.policy_snapshot = make_true_integration_policy_snapshot("desktop_full");
      const auto init_result = facade.init(init_request);
      assert_true(init_result.accepted,
            std::string{"structured terminal contract case should initialize AgentFacade: "} +
              test_case.case_id);

      const auto result = facade.handle(make_agent_request(
        "req-027-" + test_case.case_id,
        "session-027-" + test_case.case_id,
        "trace-027-" + test_case.case_id,
        "query " + test_case.case_id));

      assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
            std::string{"structured terminal decision should complete runtime: "} +
              test_case.case_id);
      assert_true(result.task_completed.value_or(false),
            std::string{"structured terminal decision should keep task_completed=true: "} +
              test_case.case_id);
      assert_true(structured_engine->decide_calls == 1,
            std::string{"runtime should consume one projected terminal ActionDecision: "} +
              test_case.case_id);
      assert_true(structured_engine->reflect_calls == 0,
            std::string{"terminal decisions should skip reflection re-entry: "} +
              test_case.case_id);
      assert_true(contract_tool_manager->invoke_calls == 0,
            std::string{"terminal structured decisions must not dispatch tools: "} +
              test_case.case_id);
      assert_true(contract_response_builder->build_calls == 1,
            std::string{"structured terminal decision should invoke response builder once: "} +
              test_case.case_id);
      assert_true(contract_response_builder->last_request.has_value() &&
              contract_response_builder->last_request->terminal_decision.has_value(),
            std::string{"structured terminal decision should reach response builder with a terminal_decision: "} +
              test_case.case_id);
      assert_true(!contract_response_builder->last_request->latest_observation.has_value(),
            std::string{"structured terminal decision should build without a tool observation: "} +
              test_case.case_id);
      assert_true(contract_response_builder->last_request->terminal_decision->decision_kind ==
              test_case.expected_kind,
            std::string{"runtime should preserve projected terminal decision kind: "} +
              test_case.case_id);
      assert_true(contract_response_builder->last_request->terminal_decision->response_outline.has_value() &&
              contract_response_builder->last_request->terminal_decision->response_outline->summary ==
                test_case.expected_summary,
            std::string{"runtime should forward the projected response outline summary: "} +
              test_case.case_id);
      assert_true(result.response_text.has_value() &&
              result.response_text->find(test_case.expected_summary) != std::string::npos,
            std::string{"response builder output should surface the projected terminal summary: "} +
              test_case.case_id);

      if (dependency_set->memory_manager != nullptr) {
        dependency_set->memory_manager->shutdown();
      }
      cleanup_database_artifacts(database_path);
      }
    }

}  // namespace

int main() {
  try {
    test_action_decision_execute_action_maps_to_runtime_progress();
    test_non_executable_decision_is_rejected_by_runtime_contract();
    test_terminal_decisions_map_to_runtime_responding();
    test_no_decision_fails_fast_before_response_builder();
    test_belief_writeback_failure_does_not_override_completed_result();
    test_reflection_continue_retry_and_replan_reenter_runtime_paths();
    test_reflection_abort_safe_stops_mainline();
    test_structured_execute_action_is_consumed_by_runtime_tool_handoff();
    test_structured_terminal_decisions_are_consumed_by_runtime_response_builder();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

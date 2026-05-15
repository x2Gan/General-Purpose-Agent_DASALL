#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../../cognition/include/CognitionConfig.h"
#include "../../../cognition/include/CognitionTypes.h"
#include "../../../cognition/include/ICognitionEngine.h"
#include "../../../cognition/include/IResponseBuilder.h"
#include "../../../cognition/include/belief/BeliefUpdateHint.h"
#include "../../../cognition/include/decision/ActionDecision.h"
#include "agent/AgentResult.h"
#include "agent/GoalContract.h"
#include "checkpoint/ReflectionDecision.h"
#include "observation/ObservationSource.h"

#include "MockLLMManager.h"

namespace dasall::tests::mocks {

struct MockCognitionFixtureOptions {
  std::string caller_domain = "runtime.agent_orchestrator";
  std::string request_id = "req-cognition-fixture";
  std::string trace_id = "trace-cognition-fixture";
  std::string profile_id = "desktop_full";
  std::string goal_id = "goal-cognition-fixture";
  std::string user_turn = "inspect the current cognition request";
  std::string goal_summary = "produce a valid cognition-facing request shape";
  std::vector<std::string> recent_history = {
      "runtime prepared a context packet for cognition",
  };
  std::vector<std::string> active_tools = {
      "agent.dataset",
  };
  std::string observation_id = "obs-cognition-fixture";
  dasall::contracts::ObservationSource observation_source =
      dasall::contracts::ObservationSource::ToolExecution;
  bool observation_success = true;
  std::string observation_payload = R"({"status":"ok"})";
  std::string selected_node_id = "plan-node:fixture";
  std::string tool_name = "agent.dataset";
  std::string response_text = "fixture-built response";
  std::int64_t base_timestamp_ms = 1712746800000LL;
};

struct FailureProfileScenario {
  std::string scenario_id;
  std::string profile_id;
  std::string stage_name;
  bool template_fallback_expected = false;
};

enum class StructuredPlanningPayloadScenario {
    Valid,
    MalformedJson,
    SchemaInvalidActionKindHint,
    ProjectionInvalidDuplicateNode,
};

enum class StructuredExecutionPayloadScenario {
    ValidDirectResponse,
    ValidExecuteAction,
    MalformedJson,
    SchemaInvalidDecisionKind,
    ProjectionInvalidToolIntentOnDirectResponse,
    ProjectionInvalidMissingSelectedNode,
};

class MockCognitionFixture {
 public:
  explicit MockCognitionFixture(MockCognitionFixtureOptions options = {})
      : options_(std::move(options)),
        llm_manager_(std::make_shared<MockLLMManager>()) {}

  [[nodiscard]] const MockCognitionFixtureOptions& options() const { return options_; }

  [[nodiscard]] std::shared_ptr<MockLLMManager> llm_manager() const {
    return llm_manager_;
  }

  [[nodiscard]] std::shared_ptr<dasall::llm::ILLMManager> llm_manager_port() const {
    return llm_manager_;
  }

  [[nodiscard]] dasall::contracts::GoalContract make_goal_contract() const {
    dasall::contracts::GoalContract goal_contract;
    goal_contract.goal_id = options_.goal_id;
    goal_contract.request_id = options_.request_id;
    goal_contract.goal_description =
        std::string{"stabilize cognition-facing runtime fixtures"};
    goal_contract.success_criteria =
        std::string{"boundary validation accepts the generated request"};
    goal_contract.status = dasall::contracts::GoalStatus::Active;
    goal_contract.created_at = options_.base_timestamp_ms;
    goal_contract.tags = std::vector<std::string>{
        "tests",
        "cognition",
        "runtime-fixture",
    };
    return goal_contract;
  }

  [[nodiscard]] dasall::contracts::ContextPacket make_context_packet() const {
    dasall::contracts::ContextPacket context_packet;
    context_packet.request_id = options_.request_id;
    context_packet.user_turn = options_.user_turn;
    context_packet.current_goal_summary = options_.goal_summary;
    context_packet.recent_history = options_.recent_history;
    context_packet.active_tools = options_.active_tools;
    context_packet.created_at = options_.base_timestamp_ms + 1;
    context_packet.tags = std::vector<std::string>{
        "tests",
        "cognition",
        "context",
    };
    return context_packet;
  }

  [[nodiscard]] dasall::contracts::BeliefState make_belief_state() const {
    dasall::contracts::BeliefState belief_state;
    belief_state.request_id = options_.request_id;
    belief_state.confirmed_facts = std::vector<std::string>{
        "runtime caller shape matches cognition public surface",
    };
    belief_state.hypotheses = std::vector<std::string>{
        "llm bridge will consume the staged manager result",
    };
    belief_state.assumptions = std::vector<std::string>{
        "runtime controls recovery and external execution",
    };
    belief_state.evidence_refs = std::vector<std::string>{
        "tests:mock-cognition-fixture",
    };
    belief_state.confidence = 0.82F;
    belief_state.goal_id = options_.goal_id;
    belief_state.created_at = options_.base_timestamp_ms + 2;
    belief_state.tags = std::vector<std::string>{
        "tests",
        "cognition",
        "belief",
    };
    return belief_state;
  }

  [[nodiscard]] dasall::contracts::Observation make_observation(
      std::optional<bool> success_override = std::nullopt,
      std::optional<std::string> payload_override = std::nullopt) const {
    dasall::contracts::Observation observation;
    observation.observation_id = options_.observation_id;
    observation.source = options_.observation_source;
    observation.success = success_override.value_or(options_.observation_success);
    observation.payload = payload_override.value_or(options_.observation_payload);
    observation.created_at = options_.base_timestamp_ms + 3;
    observation.request_id = options_.request_id;
    observation.goal_id = options_.goal_id;
    observation.tags = std::vector<std::string>{
        "tests",
        "cognition",
        "observation",
    };
    return observation;
  }

  [[nodiscard]] dasall::cognition::decision::ActionDecision make_action_decision(
      dasall::cognition::decision::ActionDecisionKind decision_kind =
          dasall::cognition::decision::ActionDecisionKind::ExecuteAction) const {
    dasall::cognition::decision::ActionDecision action_decision;
    action_decision.decision_kind = decision_kind;
    action_decision.selected_node_id = options_.selected_node_id;
    action_decision.rationale =
        std::string{"fixture-generated action decision stays on the runtime caller seam"};
    action_decision.confidence = 0.80F;
    action_decision.tool_intent_hint = dasall::cognition::decision::ToolIntentHint{
        .tool_name = options_.tool_name,
        .intent_summary = std::string{"query runtime-visible data through tool governance"},
        .argument_hints = {std::string{"query=current_state"}},
        .evidence_refs = {std::string{"tests:mock-cognition-fixture"}},
    };
    action_decision.response_outline = dasall::cognition::decision::ResponseOutline{
        .summary = std::string{"summarize the governed result back to runtime"},
        .key_points = {std::string{"preserve runtime ownership"}},
    };
    action_decision.candidate_scores = {
        dasall::cognition::decision::CandidateDecisionScore{
            .candidate_name = "execute_action",
            .score = 0.80F,
            .rationale = std::string{"fixture defaults to a governed action"},
        },
        dasall::cognition::decision::CandidateDecisionScore{
            .candidate_name = "direct_response",
            .score = 0.25F,
            .rationale = std::string{"tool-assisted path remains preferred"},
        },
    };
    return action_decision;
  }

  [[nodiscard]] dasall::cognition::CognitionStepRequest make_decide_request(
      bool include_latest_observation = false) const {
    dasall::cognition::CognitionStepRequest request;
    request.caller_domain = options_.caller_domain;
    request.request_id = options_.request_id;
    request.trace_id = options_.trace_id;
    request.profile_id = options_.profile_id;
    request.goal_contract = make_goal_contract();
    request.context_packet = make_context_packet();
    request.belief_state = make_belief_state();
    request.budget_context = dasall::cognition::BudgetContext{
        .total_budget_tokens = 2048U,
        .consumed_tokens = 256U,
        .remaining_tokens = 1792U,
        .budget_utilization = 0.125F,
        .context_was_truncated = false,
        .near_budget_limit = false,
    };
    if (include_latest_observation) {
      request.latest_observation = make_observation();
    }
    return request;
  }

  [[nodiscard]] dasall::cognition::ReflectionRequest make_reflection_request(
      std::optional<dasall::contracts::Observation> latest_observation = std::nullopt) const {
    dasall::cognition::ReflectionRequest request;
    request.caller_domain = options_.caller_domain;
    request.request_id = options_.request_id;
    request.trace_id = options_.trace_id;
    request.profile_id = options_.profile_id;
    request.goal_contract = make_goal_contract();
    request.context_packet = make_context_packet();
    request.belief_state = make_belief_state();
    request.latest_observation = latest_observation.value_or(make_observation());
    request.active_plan_ref = options_.selected_node_id;
    return request;
  }

  [[nodiscard]] dasall::cognition::ResponseBuildRequest make_response_request(
      std::optional<dasall::cognition::decision::ActionDecision> terminal_decision =
          std::nullopt,
      std::optional<dasall::contracts::Observation> latest_observation = std::nullopt) const {
    dasall::cognition::ResponseBuildRequest request;
    request.caller_domain = options_.caller_domain;
    request.request_id = options_.request_id;
    request.trace_id = options_.trace_id;
    request.profile_id = options_.profile_id;
    request.goal_contract = make_goal_contract();
    request.context_packet = make_context_packet();
    request.belief_state = make_belief_state();
    request.latest_observation = latest_observation.value_or(make_observation());
    request.terminal_decision = terminal_decision.value_or(make_action_decision());
    request.build_hints = dasall::cognition::ResponseBuildHints{
        .prefer_template = false,
        .allow_template_fallback = true,
        .max_summary_chars = 512U,
        .required_sections = {std::string{"summary"}},
    };
    return request;
  }

  [[nodiscard]] dasall::cognition::CognitionDecisionResult make_decision_result(
      dasall::cognition::decision::ActionDecisionKind decision_kind =
          dasall::cognition::decision::ActionDecisionKind::ExecuteAction) const {
    dasall::cognition::CognitionDecisionResult result;
    result.action_decision = make_action_decision(decision_kind);
    result.belief_update_hint = dasall::cognition::belief::BeliefUpdateHint{
        .confirmed_facts_delta = {
            dasall::cognition::belief::FactDelta{
                .fact = std::string{"mock cognition fixture emitted a decision"},
            },
        },
        .hypotheses_delta = {},
        .assumptions_delta = {},
        .evidence_refs_delta = {
            dasall::cognition::belief::EvidenceRefDelta{
                .evidence_ref = std::string{"tests:mock-cognition-fixture"},
            },
        },
        .missing_evidence_refs = {},
        .confidence_hint = 0.80F,
        .merge_mode = dasall::cognition::belief::BeliefMergeMode::Append,
    };
    result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = true,
        .context_confidence = 0.90F,
        .missing_evidence_hints = {},
        .recommend_context_reload = false,
    };
    result.diagnostics.push_back("mock_fixture_decision");
    return result;
  }

    [[nodiscard]] std::string make_structured_planning_payload(
            StructuredPlanningPayloadScenario scenario =
                    StructuredPlanningPayloadScenario::Valid) const {
        switch (scenario) {
            case StructuredPlanningPayloadScenario::Valid:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.plan.v1\","
                        + "\"plan_id\":\"plan-structured-bridge\","
                        + "\"revision\":1,"
                        + "\"nodes\":[{"
                        + "\"node_id\":\"" + options_.selected_node_id + "\","
                        + "\"objective\":\"collect governed evidence from the dataset tool\","
                        + "\"success_signal\":\"evidence_collected\","
                        + "\"action_kind_hint\":\"tool_action\","
                        + "\"depends_on\":[],"
                        + "\"evidence_refs\":[\"belief:evidence:structured-plan\"]}],"
                        + "\"edges\":[],"
                        + "\"open_questions\":[],"
                        + "\"plan_rationale\":\"bridge payload should become the active plan graph\","
                        + "\"estimated_complexity\":1}"
                        ;
            case StructuredPlanningPayloadScenario::MalformedJson:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.plan.v1\","
                        + "\"plan_id\":\"plan-structured-bridge\","
                        + "\"revision\":1,"
                        + "\"nodes\":[{"
                        + "\"node_id\":\"" + options_.selected_node_id + "\","
                        + "\"objective\":\"malformed planning payload should fail parsing\"";
            case StructuredPlanningPayloadScenario::SchemaInvalidActionKindHint:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.plan.v1\","
                        + "\"plan_id\":\"plan-structured-bridge\","
                        + "\"revision\":1,"
                        + "\"nodes\":[{"
                        + "\"node_id\":\"" + options_.selected_node_id + "\","
                        + "\"objective\":\"collect governed evidence from the dataset tool\","
                        + "\"success_signal\":\"evidence_collected\","
                        + "\"action_kind_hint\":\"respond_now\","
                        + "\"depends_on\":[],"
                        + "\"evidence_refs\":[\"belief:evidence:structured-plan\"]}],"
                        + "\"edges\":[],"
                        + "\"open_questions\":[],"
                        + "\"plan_rationale\":\"invalid nested enum should fail schema validation\","
                        + "\"estimated_complexity\":1}"
                        ;
            case StructuredPlanningPayloadScenario::ProjectionInvalidDuplicateNode:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.plan.v1\","
                        + "\"plan_id\":\"plan-structured-bridge\","
                        + "\"revision\":1,"
                        + "\"nodes\":[{"
                        + "\"node_id\":\"" + options_.selected_node_id + "\","
                        + "\"objective\":\"collect governed evidence from the dataset tool\","
                        + "\"success_signal\":\"evidence_collected\","
                        + "\"action_kind_hint\":\"tool_action\","
                        + "\"depends_on\":[],"
                        + "\"evidence_refs\":[\"belief:evidence:structured-plan\"]},{"
                        + "\"node_id\":\"" + options_.selected_node_id + "\","
                        + "\"objective\":\"duplicate node should fail projection\","
                        + "\"success_signal\":\"duplicate_detected\","
                        + "\"action_kind_hint\":\"validation\","
                        + "\"depends_on\":[],"
                        + "\"evidence_refs\":[\"belief:evidence:duplicate\"]}],"
                        + "\"edges\":[],"
                        + "\"open_questions\":[],"
                        + "\"plan_rationale\":\"duplicate node ids should fail projection\","
                        + "\"estimated_complexity\":1}"
                        ;
        }

        return {};
    }

    [[nodiscard]] std::string make_structured_execution_payload(
            StructuredExecutionPayloadScenario scenario =
                    StructuredExecutionPayloadScenario::ValidDirectResponse) const {
        switch (scenario) {
            case StructuredExecutionPayloadScenario::ValidDirectResponse:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.reasoning.v1\","
                        + "\"decision_kind\":\"DirectResponse\","
                        + "\"confidence\":0.79,"
                        + "\"rationale\":\"bridge execution payload should become the authoritative decision\","
                        + "\"selected_node_id\":null,"
                        + "\"tool_intent_hint\":null,"
                        + "\"clarification_needed\":false,"
                        + "\"clarification_question\":null,"
                        + "\"response_outline\":{"
                        + "\"summary\":\"" + options_.response_text + "\","
                        + "\"key_points\":[\"respond from the bridge payload\",\"skip local execute_action routing\"]},"
                        + "\"candidate_scores\":[{"
                        + "\"candidate_name\":\"direct_response\","
                        + "\"score\":0.79,"
                        + "\"rationale\":\"bridge payload selected direct response\"},{"
                        + "\"candidate_name\":\"execute_action\","
                        + "\"score\":0.21,"
                        + "\"rationale\":\"local execution should not win once projection succeeds\"}]}"
                        ;
            case StructuredExecutionPayloadScenario::ValidExecuteAction:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.reasoning.v1\","
                        + "\"decision_kind\":\"ExecuteAction\","
                        + "\"confidence\":0.82,"
                        + "\"rationale\":\"bridge execution payload should preserve governed tool intent\","
                        + "\"selected_node_id\":\"" + options_.selected_node_id + "\","
                        + "\"tool_intent_hint\":{"
                        + "\"tool_name\":\"" + options_.tool_name + "\","
                        + "\"intent_summary\":\"query runtime-visible data through tool governance\","
                        + "\"argument_hints\":[\"query=current_state\"],"
                        + "\"evidence_refs\":[\"tests:mock-cognition-fixture\"]},"
                        + "\"clarification_needed\":false,"
                        + "\"clarification_question\":null,"
                        + "\"response_outline\":null,"
                        + "\"candidate_scores\":[{"
                        + "\"candidate_name\":\"execute_action\","
                        + "\"score\":0.82,"
                        + "\"rationale\":\"bridge payload selected execute action\"}]}"
                        ;
            case StructuredExecutionPayloadScenario::MalformedJson:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.reasoning.v1\","
                        + "\"decision_kind\":\"DirectResponse\","
                        + "\"confidence\":0.79,"
                        + "\"rationale\":\"malformed execution payload should fail parsing\"";
            case StructuredExecutionPayloadScenario::SchemaInvalidDecisionKind:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.reasoning.v1\","
                        + "\"decision_kind\":\"LaunchAction\","
                        + "\"confidence\":0.79,"
                        + "\"rationale\":\"invalid enum should fail schema validation\","
                        + "\"selected_node_id\":null,"
                        + "\"tool_intent_hint\":null,"
                        + "\"clarification_needed\":false,"
                        + "\"clarification_question\":null,"
                        + "\"response_outline\":null,"
                        + "\"candidate_scores\":[{"
                        + "\"candidate_name\":\"direct_response\","
                        + "\"score\":0.79,"
                        + "\"rationale\":\"invalid enum should fail before projection\"}]}"
                        ;
            case StructuredExecutionPayloadScenario::ProjectionInvalidToolIntentOnDirectResponse:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.reasoning.v1\","
                        + "\"decision_kind\":\"DirectResponse\","
                        + "\"confidence\":0.79,"
                        + "\"rationale\":\"response decisions must not carry executable tool intent\","
                        + "\"selected_node_id\":null,"
                        + "\"tool_intent_hint\":{"
                        + "\"tool_name\":\"" + options_.tool_name + "\","
                        + "\"intent_summary\":\"this should trigger invariant failure\","
                        + "\"argument_hints\":[],"
                        + "\"evidence_refs\":[]},"
                        + "\"clarification_needed\":false,"
                        + "\"clarification_question\":null,"
                        + "\"response_outline\":{"
                        + "\"summary\":\"" + options_.response_text + "\","
                        + "\"key_points\":[\"this payload should fail closed\"]},"
                        + "\"candidate_scores\":[{"
                        + "\"candidate_name\":\"direct_response\","
                        + "\"score\":0.79,"
                        + "\"rationale\":\"invalid because tool_intent_hint is present\"}]}"
                        ;
            case StructuredExecutionPayloadScenario::ProjectionInvalidMissingSelectedNode:
                return std::string{"{"}
                        + "\"schema_version\":\"cognition.reasoning.v1\","
                        + "\"decision_kind\":\"ExecuteAction\","
                        + "\"confidence\":0.82,"
                        + "\"rationale\":\"execute_action requires a selected node id\","
                        + "\"selected_node_id\":null,"
                        + "\"tool_intent_hint\":{"
                        + "\"tool_name\":\"" + options_.tool_name + "\","
                        + "\"intent_summary\":\"missing selected node should fail invariants\","
                        + "\"argument_hints\":[],"
                        + "\"evidence_refs\":[]},"
                        + "\"clarification_needed\":false,"
                        + "\"clarification_question\":null,"
                        + "\"response_outline\":null,"
                        + "\"candidate_scores\":[{"
                        + "\"candidate_name\":\"execute_action\","
                        + "\"score\":0.82,"
                        + "\"rationale\":\"invalid because selected_node_id is missing\"}]}"
                        ;
        }

        return {};
    }

    [[nodiscard]] dasall::llm::LLMManagerResult make_structured_planning_stage_result(
            StructuredPlanningPayloadScenario scenario =
                    StructuredPlanningPayloadScenario::Valid) const {
        return MockLLMManager::make_structured_stage_result(
                "planning", make_structured_planning_payload(scenario), options_.request_id);
    }

    [[nodiscard]] dasall::llm::LLMManagerResult make_structured_execution_stage_result(
            StructuredExecutionPayloadScenario scenario =
                    StructuredExecutionPayloadScenario::ValidDirectResponse) const {
        return MockLLMManager::make_structured_stage_result(
                "execution", make_structured_execution_payload(scenario), options_.request_id);
    }

    void stage_structured_planning_result(
            StructuredPlanningPayloadScenario scenario =
                    StructuredPlanningPayloadScenario::Valid) const {
        llm_manager_->set_stage_result("planning", make_structured_planning_stage_result(scenario));
    }

    void stage_structured_execution_result(
            StructuredExecutionPayloadScenario scenario =
                    StructuredExecutionPayloadScenario::ValidDirectResponse) const {
        llm_manager_->set_stage_result("execution", make_structured_execution_stage_result(scenario));
    }

  [[nodiscard]] dasall::cognition::ResponseBuildResult make_response_result(
      bool fallback_used = false) const {
    dasall::contracts::AgentResult agent_result;
    agent_result.result_id = std::string{"agent-result-"} + options_.request_id;
    agent_result.status = fallback_used
                              ? dasall::contracts::AgentResultStatus::PartiallyCompleted
                              : dasall::contracts::AgentResultStatus::Completed;
    agent_result.result_code = 0;
    agent_result.response_text = options_.response_text;
    agent_result.task_completed = !fallback_used;
    agent_result.created_at = options_.base_timestamp_ms + 4;
    agent_result.request_id = options_.request_id;
    agent_result.trace_id = options_.trace_id;
    agent_result.structured_payload = options_.observation_payload;
    agent_result.goal_id = options_.goal_id;
    agent_result.tags = std::vector<std::string>{
        "tests",
        "cognition",
        fallback_used ? "fallback" : "success",
    };

    dasall::cognition::ResponseBuildResult result;
    result.agent_result = std::move(agent_result);
    result.fallback_used = fallback_used;
    result.diagnostics.push_back(fallback_used ? "mock_fixture_fallback"
                                               : "mock_fixture_success");
    return result;
  }

  [[nodiscard]] std::shared_ptr<dasall::cognition::ICognitionEngine> make_engine(
      const dasall::cognition::CognitionConfig& config = {}) const {
    auto engine = dasall::cognition::create_cognition_engine(
        config,
        dasall::cognition::CognitionRuntimeDependencies{
            .llm_manager = llm_manager_,
        });
    return std::shared_ptr<dasall::cognition::ICognitionEngine>(std::move(engine));
  }

  [[nodiscard]] std::shared_ptr<dasall::cognition::IResponseBuilder>
  make_response_builder(const dasall::cognition::CognitionConfig& config = {}) const {
    auto builder = dasall::cognition::create_response_builder(
        config,
        dasall::cognition::CognitionRuntimeDependencies{
            .llm_manager = llm_manager_,
        });
    return std::shared_ptr<dasall::cognition::IResponseBuilder>(std::move(builder));
  }

  [[nodiscard]] std::vector<FailureProfileScenario> default_failure_profile_scenarios() const {
    return {
        FailureProfileScenario{
            .scenario_id = "llm_unavailable",
            .profile_id = options_.profile_id,
            .stage_name = "execution",
            .template_fallback_expected = false,
        },
        FailureProfileScenario{
            .scenario_id = "response_fallback",
            .profile_id = std::string{"edge_minimal"},
            .stage_name = "response",
            .template_fallback_expected = true,
        },
    };
  }

 private:
  MockCognitionFixtureOptions options_;
  std::shared_ptr<MockLLMManager> llm_manager_;
};

}  // namespace dasall::tests::mocks

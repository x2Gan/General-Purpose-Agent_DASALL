#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "IResponseBuilder.h"
#include "MockLLMManager.h"
#include "observation/ObservationSource.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::CognitionRuntimeDependencies;
using dasall::cognition::ResponseBuildHints;
using dasall::cognition::ResponseBuildRequest;
using dasall::cognition::create_response_builder;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::GoalStatus;
using dasall::contracts::ObservationSource;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_token(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] dasall::contracts::GoalContract make_goal_contract() {
  dasall::contracts::GoalContract goal_contract;
  goal_contract.goal_id = std::string("goal-019-template");
  goal_contract.request_id = std::string("req-019-template");
  goal_contract.goal_description =
      std::string("return a safe degraded response when the observation payload is absent");
  goal_contract.success_criteria = std::string("emit an explicit template-based response or fail loudly");
  goal_contract.status = GoalStatus::Active;
  goal_contract.created_at = 1712432100000;
  return goal_contract;
}

[[nodiscard]] dasall::contracts::ContextPacket make_context_packet() {
  dasall::contracts::ContextPacket context_packet;
  context_packet.request_id = std::string("req-019-template");
  context_packet.user_turn = std::string("Give me the safe degraded answer");
  context_packet.current_goal_summary =
      std::string("emit a template-based summary when no payload is available");
  context_packet.recent_history =
      std::vector<std::string>{std::string("runtime reached a terminal response path")};
  return context_packet;
}

[[nodiscard]] dasall::contracts::BeliefState make_belief_state() {
  dasall::contracts::BeliefState belief_state;
  belief_state.request_id = std::string("req-019-template");
  belief_state.confirmed_facts =
      std::vector<std::string>{std::string("runtime can accept a degraded response")};
  belief_state.hypotheses =
      std::vector<std::string>{std::string("response builder may need a template fallback")};
  belief_state.assumptions =
      std::vector<std::string>{std::string("template summaries stay within allowed bounds")};
  belief_state.evidence_refs =
      std::vector<std::string>{std::string("decision:019-template")};
  belief_state.confidence = 0.74F;
  belief_state.goal_id = std::string("goal-019-template");
  belief_state.created_at = 1712432100100;
  return belief_state;
}

[[nodiscard]] dasall::cognition::decision::ActionDecision make_terminal_decision() {
  dasall::cognition::decision::ActionDecision decision;
  decision.decision_kind = dasall::cognition::decision::ActionDecisionKind::ConvergeSafe;
  decision.confidence = 0.67F;
  decision.rationale = std::string("template fallback is the only stable terminal output path");
  decision.response_outline = dasall::cognition::decision::ResponseOutline{
      .summary = std::string(
          "The dataset response is temporarily unavailable, so the agent is returning a safe degraded summary while preserving the terminal goal state."),
      .key_points = {std::string("degraded response"), std::string("terminal state preserved")},
  };
  return decision;
}

[[nodiscard]] dasall::contracts::Observation make_observation() {
  dasall::contracts::Observation observation;
  observation.observation_id = std::string{"obs-031-bridge-failure"};
  observation.source = ObservationSource::ToolExecution;
  observation.success = true;
  observation.payload = std::string{"{\"status\":\"ok\",\"summary\":\"bridge should rewrite this\"}"};
  observation.created_at = 1712432100200;
  observation.request_id = std::string{"req-019-template"};
  observation.goal_id = std::string{"goal-019-template"};
  return observation;
}

[[nodiscard]] ResponseBuildRequest make_request(bool allow_template_fallback = true) {
  ResponseBuildRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-019-template";
  request.trace_id = "trace-019-template";
  request.profile_id = "edge_balanced";
  request.goal_contract = make_goal_contract();
  request.context_packet = make_context_packet();
  request.belief_state = make_belief_state();
  request.latest_observation.reset();
  request.terminal_decision = make_terminal_decision();
  request.build_hints = ResponseBuildHints{
      .prefer_template = false,
      .allow_template_fallback = allow_template_fallback,
      .max_summary_chars = 72U,
      .required_sections = {std::string("summary")},
  };
  return request;
}

void test_response_builder_uses_template_fallback_when_payload_is_absent() {
  auto builder = create_response_builder();

  const auto result = builder->build(make_request());

  assert_true(result.agent_result.has_value(),
              "template fallback path should still materialize a degraded AgentResult");
  assert_true(result.fallback_used,
              "template fallback path should set fallback_used=true");
  assert_true(contains_token(result.diagnostics, "response_template_fallback"),
              "template fallback diagnostic should be emitted");
  assert_true(contains_token(result.diagnostics, "response_clamped"),
              "template fallback path should report summary clamping when max_summary_chars is set");

  const auto& agent_result = *result.agent_result;
  assert_true(agent_result.status == AgentResultStatus::PartiallyCompleted,
              "template fallback should mark the AgentResult as PartiallyCompleted");
  assert_true(!agent_result.task_completed.value_or(false),
              "template fallback should not mark the task as fully completed");
  assert_true(agent_result.response_text.has_value() &&
                  agent_result.response_text->size() <= 72U,
              "template fallback should honor max_summary_chars clamping");
  assert_true(agent_result.response_text.has_value() &&
                  agent_result.response_text->find("...") != std::string::npos,
              "clamped template fallback output should end with an ellipsis");
  assert_true(agent_result.structured_payload.has_value() &&
                  agent_result.structured_payload->find("\"fallback_used\":true") !=
                      std::string::npos,
              "structured payload should preserve the fallback flag");
}

void test_response_builder_returns_explicit_error_when_template_fallback_is_disabled() {
  auto builder = create_response_builder(CognitionConfig{});

  const auto result = builder->build(make_request(false));

  assert_true(!result.agent_result.has_value(),
              "disabled template fallback should not synthesize a degraded AgentResult");
  assert_true(result.result_code.has_value(),
              "disabled template fallback should surface an explicit result code");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::PolicyDenied),
               static_cast<int>(*result.result_code),
               "disabled template fallback should use the policy-denied result code");
  assert_true(result.error_info.has_value(),
              "disabled template fallback should surface an ErrorInfo payload");
  assert_equal(std::string("cognition.response.mode_selection"),
               result.error_info->details.stage,
               "disabled template fallback should identify the mode-selection stage");
}

void test_response_builder_falls_back_when_bridge_returns_failure() {
  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_stage_result(
      "response",
      MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "provider timeout from response stage",
          dasall::llm::LLMFailureCategory::AdapterTransport,
          "mock.route.response"));
  auto builder = create_response_builder(
      CognitionConfig{},
      CognitionRuntimeDependencies{
          .llm_manager = llm_manager,
          .policy_snapshot = nullptr,
        .logger = nullptr,
        .audit_logger = nullptr,
        .metrics_provider = nullptr,
        .tracer_provider = nullptr,
      });
  auto request = make_request(true);
  request.latest_observation = make_observation();

  const auto result = builder->build(request);

  assert_equal(1,
               llm_manager->generate_call_count(),
               "response builder should call the llm manager through the bridge once");
  assert_true(llm_manager->last_request().has_value() &&
                  llm_manager->last_request()->stage == "response",
              "response builder should project the canonical response stage");
  assert_true(result.agent_result.has_value(),
              "bridge failure with template fallback should still materialize an AgentResult");
  assert_true(result.fallback_used,
              "bridge failure should explicitly mark template fallback");
  assert_true(contains_token(result.diagnostics, "route:mock.route.response"),
              "bridge failure diagnostics should preserve the resolved response route");
  assert_true(contains_token(result.diagnostics, "response_llm_bridge_failed"),
              "bridge failure should be visible in response diagnostics");
  assert_true(contains_token(result.diagnostics, "error_type:provider"),
              "bridge failure diagnostics should preserve the contract error type");
  assert_true(contains_token(result.diagnostics, "response_template_fallback"),
              "bridge failure should fall back through the existing template path");
}

}  // namespace

int main() {
  try {
    test_response_builder_uses_template_fallback_when_payload_is_absent();
    test_response_builder_returns_explicit_error_when_template_fallback_is_disabled();
    test_response_builder_falls_back_when_bridge_returns_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

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

[[nodiscard]] std::string make_structured_response_payload() {
  return std::string{"{"}
      + "\"schema_version\":\"cognition.response.v1\","
      + "\"response_mode\":\"llm_bridge\","
      + "\"summary_text\":\"bridge-authored structured response summary\","
      + "\"structured_sections\":[\"summary\"],"
      + "\"omitted_details\":[],"
      + "\"fallback_used\":false}"
      ;
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

void test_response_builder_uses_configured_clarification_template() {
  CognitionConfig config;
  config.response.templates.clarification = "CLARIFY-OVERRIDE: {summary}";
  auto builder = create_response_builder(config);
  auto request = make_request();
  request.build_hints.max_summary_chars = 0U;
  request.terminal_decision->decision_kind =
      dasall::cognition::decision::ActionDecisionKind::AskClarification;

  const auto result = builder->build(request);

  assert_true(result.agent_result.has_value(),
              "clarification template override should still produce an AgentResult");
  assert_true(result.agent_result->response_text.has_value() &&
                  result.agent_result->response_text->find("CLARIFY-OVERRIDE: ") == 0U,
              "clarification template override should drive the user-visible response text");
  assert_true(contains_token(result.diagnostics, "response_template_kind:clarification"),
              "clarification template path should identify the selected template kind");
}

void test_response_builder_uses_configured_fallback_failure_template() {
  CognitionConfig config;
  config.response.templates.fallback_failure = "FAIL-OVERRIDE: {summary}";
  auto builder = create_response_builder(config);
  auto request = make_request();
  request.build_hints.max_summary_chars = 0U;
  request.terminal_decision.reset();

  const auto result = builder->build(request);

  assert_true(result.agent_result.has_value(),
              "generic template fallback should still produce an AgentResult");
  assert_true(result.agent_result->response_text.has_value() &&
                  result.agent_result->response_text->find("FAIL-OVERRIDE: ") == 0U,
              "fallback-failure template override should drive the user-visible response text");
  assert_true(contains_token(result.diagnostics, "response_template_kind:fallback_failure"),
              "generic template fallback should identify the fallback-failure template kind");
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

  void test_response_builder_projects_profile_specific_safe_converge_templates() {
    CognitionConfig edge_minimal_config;
    edge_minimal_config.response.templates.safe_converge =
      "Returning a safe fallback response: {summary}";
    CognitionConfig factory_test_config;
    factory_test_config.response.templates.safe_converge =
      "Diagnostic safe-converge response emitted. Summary seed: {summary}";
    auto edge_minimal_builder = create_response_builder(edge_minimal_config);
    auto factory_test_builder = create_response_builder(factory_test_config);
    auto request = make_request();
    request.build_hints.prefer_template = true;
    request.build_hints.max_summary_chars = 0U;

    const auto edge_minimal_result = edge_minimal_builder->build(request);
    const auto factory_test_result = factory_test_builder->build(request);

    assert_true(edge_minimal_result.agent_result.has_value() &&
            edge_minimal_result.agent_result->response_text.has_value(),
          "edge_minimal profile should still materialize a template response");
    assert_true(factory_test_result.agent_result.has_value() &&
            factory_test_result.agent_result->response_text.has_value(),
          "factory_test profile should still materialize a template response");
    assert_true(edge_minimal_result.agent_result->response_text->find(
            "Returning a safe fallback response: ") == 0U,
          "edge_minimal should project the compact safe-converge template copy");
    assert_true(factory_test_result.agent_result->response_text->find(
            "Diagnostic safe-converge response emitted. ") == 0U,
          "factory_test should project the diagnostic safe-converge template copy");
    assert_true(*edge_minimal_result.agent_result->response_text !=
            *factory_test_result.agent_result->response_text,
          "different profiles should produce different projected template copy");
  }

void test_response_builder_consumes_structured_bridge_payload() {
  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_structured_stage_payload(
      "response",
      make_structured_response_payload(),
      std::string{"req-019-template"});
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

  assert_true(result.agent_result.has_value(),
              "valid structured bridge payload should materialize an AgentResult");
  assert_true(!result.fallback_used,
              "valid structured bridge payload should stay on the non-fallback path");
  assert_true(result.agent_result->status == AgentResultStatus::Completed,
              "valid structured bridge payload should keep the AgentResult completed");
  assert_true(contains_token(result.diagnostics, "structured_projection.projected_response_envelope"),
              "response builder should mark the structured response envelope projection");
  assert_true(result.agent_result->response_text.has_value() &&
                  *result.agent_result->response_text ==
                      "bridge-authored structured response summary",
              "response builder should consume the structured bridge summary_text");
  assert_true(result.agent_result->structured_payload.has_value() &&
                  result.agent_result->structured_payload->find("\"schema_version\":\"cognition.response.v1\"") !=
                      std::string::npos,
              "response builder should preserve the frozen response schema version in structured payload");
}

}  // namespace

int main() {
  try {
    test_response_builder_uses_template_fallback_when_payload_is_absent();
    test_response_builder_uses_configured_clarification_template();
    test_response_builder_uses_configured_fallback_failure_template();
    test_response_builder_returns_explicit_error_when_template_fallback_is_disabled();
    test_response_builder_falls_back_when_bridge_returns_failure();
    test_response_builder_projects_profile_specific_safe_converge_templates();
    test_response_builder_consumes_structured_bridge_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

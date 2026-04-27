#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition failure injection integration coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "CognitionTypes.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "RuntimeUnaryFixture.h"
#include "agent/AgentResult.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

enum class FailureMode : std::uint8_t {
  LlmUnavailable = 0,
  SchemaViolation,
  MissingBeliefState,
  ContradictoryObservation,
  ResponseFallback,
  DecisionExecuteConflict,
  ReflectionErrorPriority,
};

class FailureInjectCognitionEngine final : public dasall::cognition::ICognitionEngine {
 public:
  explicit FailureInjectCognitionEngine(FailureMode mode)
      : mode_(mode) {}

  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest&) override {
    dasall::cognition::CognitionDecisionResult result;

    if (mode_ == FailureMode::ContradictoryObservation ||
      mode_ == FailureMode::ResponseFallback ||
      mode_ == FailureMode::DecisionExecuteConflict ||
      mode_ == FailureMode::ReflectionErrorPriority) {
      dasall::cognition::decision::ActionDecision decision;
      decision.decision_kind =
          dasall::cognition::decision::ActionDecisionKind::ExecuteAction;
      decision.selected_node_id = std::string{"failure-injection-node"};
      decision.rationale = std::string{"failure injection execute decision"};
      decision.confidence = 0.78F;
      decision.tool_intent_hint = dasall::cognition::decision::ToolIntentHint{
          .tool_name = mode_ == FailureMode::ContradictoryObservation
                           ? std::string{"agent.unknown"}
                           : std::string{"agent.dataset"},
          .intent_summary = std::string{"inject runtime cognition failure mode"},
          .argument_hints = {std::string{"query=failure-injection"}},
          .evidence_refs = {std::string{"tests:cognition-failure-injection"}},
      };
      result.action_decision = std::move(decision);
      if (mode_ == FailureMode::DecisionExecuteConflict) {
        result.result_code = dasall::contracts::ResultCode::ValidationFieldMissing;

        dasall::contracts::ErrorInfo error;
        error.failure_type = dasall::contracts::classify_result_code(*result.result_code);
        error.retryable = false;
        error.safe_to_replan = false;
        error.details.code = static_cast<int>(*result.result_code);
        error.details.stage = "cognition.decide";
        error.details.message = "decision returned execute action together with an error";
        error.source_ref.ref_type = "component";
        error.source_ref.ref_id = "FailureInjectCognitionEngine";
        result.error_info = error;
      }
      return result;
    }

    result.result_code = mode_ == FailureMode::LlmUnavailable
                             ? dasall::contracts::ResultCode::ProviderTimeout
                             : dasall::contracts::ResultCode::ValidationFieldMissing;

    dasall::contracts::ErrorInfo error;
    error.failure_type = dasall::contracts::classify_result_code(*result.result_code);
    error.retryable = false;
    error.safe_to_replan = false;
    error.details.code = static_cast<int>(*result.result_code);
    error.details.stage = "cognition.decide";
    error.details.message = mode_ == FailureMode::LlmUnavailable
                                ? "provider unavailable"
                                : "schema or belief validation failure";
    error.source_ref.ref_type = "component";
    error.source_ref.ref_id = "FailureInjectCognitionEngine";
    result.error_info = error;

    dasall::cognition::decision::ActionDecision non_executable;
    non_executable.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::AskClarification;
    non_executable.confidence = 0.25F;
    non_executable.clarification_needed = true;
    non_executable.clarification_question =
        std::string{"failure injection produced a non-executable cognition result"};
    result.action_decision = std::move(non_executable);

    result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = false,
        .context_confidence = 0.2F,
        .missing_evidence_hints = {std::string{"belief_state.confirmed_facts"}},
        .recommend_context_reload = true,
    };
    return result;
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest& request) override {
    dasall::cognition::CognitionReflectionResult result;
    if (mode_ == FailureMode::ReflectionErrorPriority) {
      result.result_code = dasall::contracts::ResultCode::ProviderTimeout;

      dasall::contracts::ErrorInfo error;
      error.failure_type = dasall::contracts::classify_result_code(*result.result_code);
      error.retryable = false;
      error.safe_to_replan = false;
      error.details.code = static_cast<int>(*result.result_code);
      error.details.stage = "cognition.reflection";
      error.details.message = "reflection emitted an explicit error surface";
      error.source_ref.ref_type = "component";
      error.source_ref.ref_id = "FailureInjectCognitionEngine.reflect";
      result.error_info = error;

      dasall::contracts::ReflectionDecision reflection_decision;
      reflection_decision.request_id = request.request_id;
      reflection_decision.goal_id = request.goal_contract.goal_id;
      reflection_decision.decision_kind = dasall::contracts::ReflectionDecisionKind::AbortSafe;
      reflection_decision.rationale = std::string{"reflection error should win over abort_safe suggestion"};
      result.reflection_decision = std::move(reflection_decision);
    }
    return result;
  }

 private:
  FailureMode mode_;
};

class FallbackResponseBuilder final : public dasall::cognition::IResponseBuilder {
 public:
  [[nodiscard]] dasall::cognition::ResponseBuildResult build(
      const dasall::cognition::ResponseBuildRequest& request) override {
    dasall::contracts::AgentResult agent_result;
    agent_result.status = dasall::contracts::AgentResultStatus::PartiallyCompleted;
    agent_result.result_code = 0;
    agent_result.response_text = "response builder fallback injected";
    agent_result.task_completed = false;
    agent_result.request_id = request.request_id;
    agent_result.trace_id = request.trace_id;
    agent_result.goal_id = request.goal_contract.goal_id;

    dasall::cognition::ResponseBuildResult result;
    result.agent_result = std::move(agent_result);
    result.fallback_used = true;
    result.diagnostics.push_back("response_fallback_injected");
    return result;
  }
};

[[nodiscard]] dasall::contracts::AgentResult run_case(FailureMode mode,
                                                      const std::string& case_id,
                                                      bool inject_fallback_builder = false) {
  const auto database_path = make_temp_database_path("dasall-cognition-failure-" + case_id);
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-028-" + case_id,
      "turn-028-" + case_id + "-001",
      "query cognition failure injection " + case_id);

  dependency_set->cognition_engine = std::make_shared<FailureInjectCognitionEngine>(mode);
  if (inject_fallback_builder) {
    dependency_set->response_builder = std::make_shared<FallbackResponseBuilder>();
  }

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-028-" + case_id,
      "desktop_full",
      "cognition-failure-injection-" + case_id));
  assert_true(init_result.accepted,
              "failure injection case should initialize AgentFacade");

  const auto result = facade.handle(make_agent_request(
      "req-028-" + case_id,
      "session-028-" + case_id,
      "trace-028-" + case_id,
      "query cognition failure injection " + case_id));

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
  return result;
}

void test_failure_injection_paths() {
  const auto llm_unavailable = run_case(FailureMode::LlmUnavailable, "llm_unavailable");
  assert_true(llm_unavailable.status == dasall::contracts::AgentResultStatus::Failed,
              "llm unavailable should fail explicitly on runtime-cognition boundary");
  assert_true(llm_unavailable.error_info.has_value() &&
                  llm_unavailable.error_info->details.stage == "main_loop",
              "llm unavailable should map failure back to main_loop");

  const auto schema_violation = run_case(FailureMode::SchemaViolation, "schema_violation");
  assert_true(schema_violation.status == dasall::contracts::AgentResultStatus::Failed,
              "schema violation should fail explicitly on runtime-cognition boundary");

  const auto missing_belief = run_case(FailureMode::MissingBeliefState, "missing_belief");
  assert_true(missing_belief.status == dasall::contracts::AgentResultStatus::Failed,
              "missing belief should fail explicitly without silent retries");

  const auto contradictory_observation =
      run_case(FailureMode::ContradictoryObservation, "contradictory_observation");
  assert_true(contradictory_observation.status == dasall::contracts::AgentResultStatus::Failed,
              "contradictory observation should fail on tool projection path");
  assert_true(contradictory_observation.error_info.has_value() &&
                  contradictory_observation.error_info->details.stage == "tool_round",
              "contradictory observation should be surfaced from tool_round");

  const auto response_fallback =
      run_case(FailureMode::ResponseFallback, "response_fallback", true);
  assert_true(
      response_fallback.status == dasall::contracts::AgentResultStatus::PartiallyCompleted,
      "response fallback should return a degraded but explicit partially completed result");
  assert_true(response_fallback.response_text.has_value() &&
                  response_fallback.response_text->find("fallback") != std::string::npos,
              "response fallback case should surface explicit fallback response text");

    const auto decision_execute_conflict =
      run_case(FailureMode::DecisionExecuteConflict, "decision_execute_conflict");
    assert_true(decision_execute_conflict.status == dasall::contracts::AgentResultStatus::Failed,
          "executable action plus cognition error should fail closed at runtime boundary");
    assert_true(decision_execute_conflict.error_info.has_value() &&
            decision_execute_conflict.error_info->details.stage == "main_loop",
          "decision execute conflict should be attributed to main_loop fail-closed handling");

    const auto reflection_error_priority =
      run_case(FailureMode::ReflectionErrorPriority, "reflection_error_priority");
    assert_true(reflection_error_priority.status == dasall::contracts::AgentResultStatus::Failed,
          "reflection error surface should take priority over any recovery suggestion");
    assert_true(reflection_error_priority.error_info.has_value() &&
            reflection_error_priority.error_info->details.stage == "cognition.reflection",
          "reflection error priority should preserve the original cognition reflection stage");
    assert_true(!reflection_error_priority.response_text.has_value() ||
            reflection_error_priority.response_text->find("fail-safe") == std::string::npos,
          "reflection error priority should fail before entering the fail-safe terminal response path");
}

}  // namespace

int main() {
  try {
    test_failure_injection_paths();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}

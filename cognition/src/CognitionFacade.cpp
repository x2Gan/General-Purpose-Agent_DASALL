#include "ICognitionEngine.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "StagePolicyResolver.h"
#include "belief/BeliefUpdateSynthesizer.h"
#include "config/CognitionConfigProjector.h"
#include "llm/CognitionLlmBridge.h"
#include "observability/CognitionTelemetry.h"
#include "perception/PerceptionEngine.h"
#include "planning/Planner.h"
#include "reasoning/Reasoner.h"
#include "reflection/ReflectionEngine.h"
#include "validation/InputBoundaryValidator.h"
#include "validation/StageOutputValidator.h"

namespace dasall::cognition {
namespace {

using decision::ActionDecision;
using decision::ActionDecisionKind;
using llm_bridge::CognitionLlmBridge;
using llm_bridge::StageLlmCallResult;
using observability::DecisionTelemetryRecord;
using observability::StageTelemetryContext;
using observability::TelemetryEmitResult;
using policy::StageExecutionPlan;
using validation::InputBoundaryValidationResult;

[[nodiscard]] std::string default_response_summary(const CognitionStepRequest& request) {
  if (request.context_packet.current_goal_summary.has_value() &&
      !request.context_packet.current_goal_summary->empty()) {
    return *request.context_packet.current_goal_summary;
  }

  return *request.goal_contract.goal_description;
}

[[nodiscard]] std::string require_goal_id(const contracts::GoalContract& goal_contract) {
  return goal_contract.goal_id.value_or(std::string{});
}

[[nodiscard]] std::string derive_model_hint_tier(const CognitionStepRequest& request) {
  if (request.execution_hints.low_latency_preferred) {
    return "economy";
  }

  if (request.budget_context.has_value() && request.budget_context->near_budget_limit) {
    return "balanced";
  }

  return "standard";
}

[[nodiscard]] std::string derive_model_hint_tier(const ReflectionRequest& request) {
  if (request.context_packet.policy_digest.has_value() &&
      request.context_packet.policy_digest->find("economy") != std::string::npos) {
    return "economy";
  }

  return "standard";
}

[[nodiscard]] StageTelemetryContext make_stage_context(
    const CognitionStepRequest& request,
    std::string stage,
    bool fallback_used,
    std::optional<contracts::ResultCode> result_code = std::nullopt) {
  return StageTelemetryContext{
      .request_id = request.request_id,
      .goal_id = require_goal_id(request.goal_contract),
      .profile_id = request.profile_id,
      .stage = std::move(stage),
      .trace_id = request.trace_id,
      .model_hint_tier = derive_model_hint_tier(request),
      .fallback_used = fallback_used,
      .result_code = result_code.has_value()
                         ? std::optional<int>(static_cast<int>(*result_code))
                         : std::nullopt,
  };
}

[[nodiscard]] StageTelemetryContext make_stage_context(
    const ReflectionRequest& request,
    std::string stage,
    bool fallback_used,
    std::optional<contracts::ResultCode> result_code = std::nullopt) {
  return StageTelemetryContext{
      .request_id = request.request_id,
      .goal_id = require_goal_id(request.goal_contract),
      .profile_id = request.profile_id,
      .stage = std::move(stage),
      .trace_id = request.trace_id,
      .model_hint_tier = derive_model_hint_tier(request),
      .fallback_used = fallback_used,
      .result_code = result_code.has_value()
                         ? std::optional<int>(static_cast<int>(*result_code))
                         : std::nullopt,
  };
}

void append_unique(std::vector<std::string>& target, std::string value) {
  if (value.empty()) {
    return;
  }

  if (std::find(target.begin(), target.end(), value) == target.end()) {
    target.push_back(std::move(value));
  }
}

void append_unique(std::vector<std::string>& target, const std::vector<std::string>& values) {
  for (const auto& value : values) {
    append_unique(target, value);
  }
}

void ignore_emit_result(TelemetryEmitResult) {}

[[nodiscard]] contracts::ErrorInfo make_error_info(
    contracts::ResultCode result_code,
    std::string stage,
    std::string message,
    std::string source_component) {
  contracts::ErrorInfo error_info;
  error_info.failure_type = contracts::classify_result_code(result_code);
  error_info.retryable = false;
  error_info.safe_to_replan = false;
  error_info.details.code = static_cast<int>(result_code);
  error_info.details.stage = std::move(stage);
  error_info.details.message = std::move(message);
  error_info.source_ref.ref_type = "component";
  error_info.source_ref.ref_id = std::move(source_component);
  return error_info;
}

[[nodiscard]] bool should_recommend_context_reload(float context_confidence) {
  return context_confidence < 0.45F;
}

[[nodiscard]] std::vector<std::string> collect_missing_evidence(
    const perception::PerceptionResult& perception_result) {
  std::vector<std::string> missing_evidence;
  for (const auto& ambiguity : perception_result.ambiguities) {
    append_unique(missing_evidence, ambiguity.missing_evidence_refs);
  }
  for (const auto& clarification_question : perception_result.clarification_questions) {
    append_unique(missing_evidence, clarification_question.evidence_refs);
  }
  return missing_evidence;
}

[[nodiscard]] ActionDecision make_clarification_fallback(
    const CognitionStepRequest& request,
    std::string rationale) {
  ActionDecision decision;
  decision.decision_kind = ActionDecisionKind::AskClarification;
  decision.rationale = std::move(rationale);
  decision.confidence = std::max(0.55F, request.belief_state.confidence.value_or(0.35F));
  decision.clarification_needed = true;
  decision.clarification_question =
      "What concrete target or evidence should cognition confirm before continuing?";
  decision.response_outline = decision::ResponseOutline{
      .summary = default_response_summary(request),
      .key_points = {"Await user clarification before executing external actions."},
  };
  decision.candidate_scores.push_back(decision::CandidateDecisionScore{
      .candidate_name = "ask_clarification",
      .score = decision.confidence,
      .rationale = "fallback clarification path retained safety after perception failure",
  });
  return decision;
}

[[nodiscard]] ActionDecision make_converge_safe_fallback(
    const CognitionStepRequest& request,
    std::string rationale) {
  ActionDecision decision;
  decision.decision_kind = ActionDecisionKind::ConvergeSafe;
  decision.rationale = std::move(rationale);
  decision.confidence = std::max(0.60F, request.belief_state.confidence.value_or(0.40F));
  decision.response_outline = decision::ResponseOutline{
      .summary = default_response_summary(request),
      .key_points = {"Return a bounded response without external execution."},
  };
  decision.candidate_scores.push_back(decision::CandidateDecisionScore{
      .candidate_name = "converge_safe",
      .score = decision.confidence,
      .rationale = "fallback safe convergence preserved runtime ownership",
  });
  return decision;
}

void apply_invalid_decide_result(
    CognitionDecisionResult& result,
    const InputBoundaryValidationResult& validation_result) {
  result.result_code = contracts::ResultCode::ValidationFieldMissing;
  result.error_info = validation_result.error_info;
  result.context_sufficiency.context_sufficient = false;
  result.context_sufficiency.context_confidence = 0.0F;
  result.context_sufficiency.missing_evidence_hints = validation_result.missing_fields;
  result.context_sufficiency.recommend_context_reload = true;
  result.diagnostics.push_back("invalid_input");
}

void apply_invalid_reflection_result(
    CognitionReflectionResult& result,
    const InputBoundaryValidationResult& validation_result) {
  result.result_code = contracts::ResultCode::ValidationFieldMissing;
  result.error_info = validation_result.error_info;
  result.diagnostics.push_back("invalid_input");
}

void apply_decision_failure(
    CognitionDecisionResult& result,
    contracts::ResultCode result_code,
    contracts::ErrorInfo error_info,
    std::string diagnostic) {
  result.result_code = result_code;
  result.error_info = std::move(error_info);
  result.context_sufficiency.context_sufficient = false;
  result.context_sufficiency.context_confidence = 0.0F;
  result.context_sufficiency.recommend_context_reload = true;
  append_unique(result.diagnostics, std::move(diagnostic));
}

void apply_reflection_failure(
    CognitionReflectionResult& result,
    contracts::ResultCode result_code,
    contracts::ErrorInfo error_info,
    std::string diagnostic) {
  result.result_code = result_code;
  result.error_info = std::move(error_info);
  append_unique(result.diagnostics, std::move(diagnostic));
}

[[nodiscard]] std::string optional_text(const std::optional<std::string>& value) {
  return value.value_or(std::string{});
}

[[nodiscard]] const StageModelHint* find_stage_model_hint(
    const StageExecutionPlan& plan,
    std::string_view stage_name,
    std::string_view task_type) {
  for (const auto& hint : plan.stage_model_hints) {
    if (hint.stage_name == stage_name && hint.task_type == task_type) {
      return &hint;
    }
  }

  return nullptr;
}

[[nodiscard]] StageModelHint make_bridge_model_hint(
    std::string stage,
    std::string task_type,
    ModelCapabilityTier capability_tier,
    bool requires_structured_output,
    std::uint32_t max_output_tokens,
    std::uint32_t deadline_ms) {
  return StageModelHint{
      .stage_name = std::move(stage),
      .task_type = std::move(task_type),
      .capability_tier = capability_tier,
      .max_output_tokens = max_output_tokens,
      .deadline_ms = deadline_ms,
      .requires_structured_output = requires_structured_output,
      .requires_reasoning_trace = capability_tier == ModelCapabilityTier::Advanced ||
                                  capability_tier == ModelCapabilityTier::ReasoningHeavy,
      .cost_sensitivity = 0.0F,
      .preferred_provider = {},
  };
}

[[nodiscard]] std::vector<std::string> make_decision_stage_messages(
    const CognitionStepRequest& request,
    const std::string& stage,
    const std::string& task_type) {
  std::vector<std::string> messages;
  messages.push_back("stage=" + stage + "; task_type=" + task_type);
  messages.push_back("goal=" + optional_text(request.goal_contract.goal_description));
  messages.push_back("context=" + optional_text(request.context_packet.user_turn));
  if (request.context_packet.current_goal_summary.has_value()) {
    messages.push_back("goal_summary=" + *request.context_packet.current_goal_summary);
  }
  if (request.latest_observation.has_value() && request.latest_observation->payload.has_value()) {
    messages.push_back("latest_observation=" + *request.latest_observation->payload);
  }
  return messages;
}

[[nodiscard]] std::vector<std::string> make_reflection_stage_messages(
    const ReflectionRequest& request) {
  std::vector<std::string> messages;
  messages.push_back("stage=reflection; task_type=failure_analysis");
  messages.push_back("goal=" + optional_text(request.goal_contract.goal_description));
  messages.push_back("context=" + optional_text(request.context_packet.user_turn));
  if (request.latest_observation.payload.has_value()) {
    messages.push_back("latest_observation=" + *request.latest_observation.payload);
  }
  if (request.latest_observation.error.has_value()) {
    messages.push_back("latest_error=" + request.latest_observation.error->details.message);
  }
  return messages;
}

void append_bridge_diagnostics(std::vector<std::string>& diagnostics,
                               const StageLlmCallResult& bridge_result,
                               const std::string& stage) {
  append_unique(diagnostics, std::string{"llm_bridge.invoked:"} + stage);
  if (bridge_result.error_info.has_value()) {
    append_unique(diagnostics, std::string{"llm_bridge.failed:"} + stage);
  } else {
    append_unique(diagnostics, std::string{"llm_bridge.completed:"} + stage);
  }
  append_unique(diagnostics, bridge_result.diagnostics);
}

[[nodiscard]] DecisionTelemetryRecord make_completed_record(const ActionDecision& decision) {
  return DecisionTelemetryRecord{
      .decision_kind = decision.decision_kind,
      .confidence = decision.confidence,
      .candidate_scores = decision.candidate_scores,
      .selected_node_id = decision.selected_node_id,
      .clarification_needed = decision.clarification_needed,
      .clarification_question = decision.clarification_question,
      .response_summary = decision.response_outline.has_value()
                              ? std::optional<std::string>(decision.response_outline->summary)
                              : std::nullopt,
      .audit_refs = {},
  };
}

[[nodiscard]] DecisionTelemetryRecord make_reflection_record(
    const contracts::ReflectionDecision& decision) {
  return DecisionTelemetryRecord{
      .decision_kind = dasall::cognition::decision::ActionDecisionKind::NoDecision,
      .confidence = 0.0F,
      .candidate_scores = {},
      .selected_node_id = std::nullopt,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .response_summary = decision.rationale,
      .audit_refs = {},
  };
}

class CognitionFacade final : public ICognitionEngine {
 public:
  explicit CognitionFacade(CognitionConfig config,
                           CognitionRuntimeDependencies dependencies = {})
      : config_(config),
        perception_engine_(config),
        planner_(config),
        reasoner_(config),
        reflection_engine_(config),
        telemetry_(config),
        llm_bridge_(dependencies.llm_manager != nullptr
                        ? std::make_shared<CognitionLlmBridge>(
                              std::move(dependencies.llm_manager))
          : nullptr),
      policy_snapshot_(std::move(dependencies.policy_snapshot)) {}

  [[nodiscard]] CognitionDecisionResult decide(
      const CognitionStepRequest& request) override {
    const auto validation_result =
        validation::InputBoundaryValidator::validate_decide_request(request);
    auto telemetry_context = make_stage_context(request, "execution", false);
    ignore_emit_result(telemetry_.emit_stage_started(telemetry_context));

    if (!validation_result.ok()) {
      CognitionDecisionResult result;
      apply_invalid_decide_result(result, validation_result);
      telemetry_context.result_code =
          static_cast<int>(contracts::ResultCode::ValidationFieldMissing);
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

    auto result = run_decision_pipeline(request);
    const auto fallback_used = std::find(result.diagnostics.begin(), result.diagnostics.end(),
                                         "decision_pipeline.degraded") !=
                               result.diagnostics.end();
    telemetry_context = make_stage_context(request, "execution", fallback_used, result.result_code);

    if (result.error_info.has_value()) {
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

    if (result.action_decision.has_value()) {
      const auto record = make_completed_record(*result.action_decision);
      if (result.action_decision->decision_kind == ActionDecisionKind::AskClarification) {
        ignore_emit_result(
            telemetry_.emit_clarification_requested(telemetry_context, record));
      }
      ignore_emit_result(telemetry_.emit_stage_completed(telemetry_context, record));
    }

    return result;
  }

  [[nodiscard]] CognitionReflectionResult reflect(
      const ReflectionRequest& request) override {
    const auto validation_result =
        validation::InputBoundaryValidator::validate_reflection_request(request);
    auto telemetry_context = make_stage_context(request, "reflection", false);
    ignore_emit_result(telemetry_.emit_stage_started(telemetry_context));

    if (!validation_result.ok()) {
      CognitionReflectionResult result;
      apply_invalid_reflection_result(result, validation_result);
      telemetry_context.result_code =
          static_cast<int>(contracts::ResultCode::ValidationFieldMissing);
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

    auto result = run_reflection_pipeline(request);
    telemetry_context = make_stage_context(request, "reflection", false, result.result_code);

    if (result.error_info.has_value()) {
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

    if (result.reflection_decision.has_value()) {
      ignore_emit_result(telemetry_.emit_stage_completed(
          telemetry_context, make_reflection_record(*result.reflection_decision)));
    }

    return result;
  }

 private:
  [[nodiscard]] CognitionDecisionResult run_decision_pipeline(
      const CognitionStepRequest& request) {
    CognitionDecisionResult result;

    std::optional<StageExecutionPlan> decision_plan;
    if (policy_snapshot_ != nullptr) {
      decision_plan = policy::StagePolicyResolver::resolve_decide_plan(*policy_snapshot_, request);
      if (!decision_plan.has_value()) {
        apply_decision_failure(
            result,
            contracts::ResultCode::PolicyDenied,
            make_error_info(contracts::ResultCode::PolicyDenied,
                            "cognition.decide.policy",
                            "runtime policy snapshot could not produce a decision stage plan",
                            "cognition::policy::StagePolicyResolver"),
            "decision_pipeline.policy_projection_failed");
        return result;
      }
    }

    const auto* planning_hint = decision_plan.has_value()
                                    ? find_stage_model_hint(*decision_plan, "planning", "plan")
                                    : nullptr;
    const auto* execution_hint = decision_plan.has_value()
                                     ? find_stage_model_hint(
                                           *decision_plan, "execution", "action_decision")
                                     : nullptr;
    if (decision_plan.has_value() && (planning_hint == nullptr || execution_hint == nullptr)) {
      apply_decision_failure(
          result,
          contracts::ResultCode::PolicyDenied,
          make_error_info(contracts::ResultCode::PolicyDenied,
                          "cognition.decide.policy",
                          "runtime policy snapshot did not expose the required bridge hints",
                          "cognition::policy::StagePolicyResolver"),
          "decision_pipeline.policy_hints_missing");
      return result;
    }

    const auto max_plan_nodes =
        decision_plan.has_value() ? decision_plan->max_plan_nodes : config_.max_plan_nodes;
    const auto max_plan_depth =
        decision_plan.has_value() ? decision_plan->max_plan_depth : config_.max_plan_depth;
    const auto rule_fallback_enabled =
        request.execution_hints.degraded_path_allowed &&
        (!decision_plan.has_value() || decision_plan->rule_fallback_enabled);

    const auto perception_result = perception_engine_.perceive(request);
    if (!perception_result.has_value()) {
      if (rule_fallback_enabled) {
        result.action_decision = make_clarification_fallback(
            request,
            "decision pipeline degraded to clarification because perception produced no safe output");
        result.context_sufficiency.context_sufficient = false;
        result.context_sufficiency.context_confidence =
            request.belief_state.confidence.value_or(0.25F);
        result.context_sufficiency.recommend_context_reload = true;
        result.context_sufficiency.missing_evidence_hints = {"context_packet.user_turn"};
        belief::BeliefUpdateHint belief_update_hint;
        belief_update_hint.missing_evidence_refs = {"context_packet.user_turn"};
        belief_update_hint.confidence_hint = 0.25F;
        belief_update_hint.merge_mode = belief::BeliefMergeMode::Merge;
        result.belief_update_hint = std::move(belief_update_hint);
        append_unique(result.diagnostics, "decision_pipeline.degraded");
        append_unique(result.diagnostics, "decision_pipeline.perception_unavailable");
        return result;
      }

      apply_decision_failure(
          result,
          contracts::ResultCode::RuntimeRetryExhausted,
          make_error_info(contracts::ResultCode::RuntimeRetryExhausted,
                          "cognition.decide.perception",
                          "perception engine could not derive a safe cognition result",
                          "cognition::perception::PerceptionEngine"),
          "decision_pipeline.perception_unavailable");
      return result;
    }

    result.context_sufficiency.context_sufficient = !perception_result->requires_clarification;
    result.context_sufficiency.context_confidence = perception_result->confidence;
    result.context_sufficiency.missing_evidence_hints = collect_missing_evidence(*perception_result);
    result.context_sufficiency.recommend_context_reload =
        perception_result->requires_clarification ||
        should_recommend_context_reload(perception_result->confidence);
    append_unique(result.diagnostics, perception_result->diagnostics);

    consume_decision_bridge_stage(request,
                                  "planning",
                                  "plan",
                                  ModelCapabilityTier::Standard,
                                  true,
                                  512U,
                                  result,
                                  planning_hint);
    if (result.error_info.has_value()) {
      return result;
    }

    PlanningRequest planning_request;
    planning_request.caller_domain = request.caller_domain;
    planning_request.request_id = request.request_id;
    planning_request.trace_id = request.trace_id;
    planning_request.profile_id = request.profile_id;
    planning_request.goal_contract = request.goal_contract;
    planning_request.context_packet = request.context_packet;
    planning_request.belief_state = request.belief_state;
    planning_request.perception_result = *perception_result;
    planning_request.budget_context = request.budget_context;
    planning_request.execution_hints = request.execution_hints;
    const auto plan_graph = planner_.build_plan(planning_request);

    const auto plan_validation = validator_.validate_plan_graph_invariants(
        plan_graph, max_plan_nodes, max_plan_depth);
    if (!plan_validation.ok) {
      if (rule_fallback_enabled) {
        result.action_decision = make_clarification_fallback(
            request,
            "decision pipeline degraded to clarification because plan invariants failed validation");
        result.context_sufficiency.context_sufficient = false;
        result.context_sufficiency.context_confidence =
            std::min(result.context_sufficiency.context_confidence, 0.35F);
        result.context_sufficiency.recommend_context_reload = true;
        append_unique(result.context_sufficiency.missing_evidence_hints, "active_plan");
        append_unique(result.diagnostics, "decision_pipeline.degraded");
        append_unique(result.diagnostics, "decision_pipeline.plan_validation_failed");
        return result;
      }

      apply_decision_failure(result,
                             contracts::ResultCode::ValidationFieldMissing,
                             *plan_validation.error_info,
                             "decision_pipeline.plan_validation_failed");
      return result;
    }

    ReasoningRequest reasoning_request;
    reasoning_request.caller_domain = request.caller_domain;
    reasoning_request.request_id = request.request_id;
    reasoning_request.trace_id = request.trace_id;
    reasoning_request.profile_id = request.profile_id;
    reasoning_request.goal_contract = request.goal_contract;
    reasoning_request.context_packet = request.context_packet;
    reasoning_request.belief_state = request.belief_state;
    reasoning_request.perception_result = *perception_result;
    reasoning_request.active_plan = plan_graph;
    reasoning_request.latest_observation = request.latest_observation;
    reasoning_request.budget_context = request.budget_context;
    reasoning_request.execution_hints = request.execution_hints;

    consume_decision_bridge_stage(request,
                                  "execution",
                                  "action_decision",
                                  ModelCapabilityTier::Standard,
                                  true,
                                  256U,
                                  result,
                                  execution_hint);
    if (result.error_info.has_value()) {
      return result;
    }

    const auto action_decision = reasoner_.decide(reasoning_request);

    const auto decision_validation =
        validator_.validate_action_decision_invariants(action_decision);
    if (!decision_validation.ok) {
      if (request.execution_hints.degraded_path_allowed) {
        result.action_decision = make_converge_safe_fallback(
            request,
            "decision pipeline converged safe because decision invariants failed validation");
        append_unique(result.diagnostics, "decision_pipeline.degraded");
        append_unique(result.diagnostics, "decision_pipeline.action_validation_failed");
      } else {
        apply_decision_failure(result,
                               contracts::ResultCode::ValidationFieldMissing,
                               *decision_validation.error_info,
                               "decision_pipeline.action_validation_failed");
        return result;
      }
    } else {
      result.action_decision = action_decision;
    }

    result.belief_update_hint = belief_update_synthesizer_.synthesize_from_decide(
        *perception_result, *result.action_decision, request.latest_observation);
    append_unique(result.diagnostics, "decision_pipeline.completed");
    return result;
  }

  [[nodiscard]] CognitionReflectionResult run_reflection_pipeline(
      const ReflectionRequest& request) {
    CognitionReflectionResult result;

    const auto reflection_plan = policy_snapshot_ != nullptr
                                     ? policy::StagePolicyResolver::resolve_reflection_plan(
                                           *policy_snapshot_, request)
                                     : std::optional<StageExecutionPlan>{};
    if (policy_snapshot_ != nullptr && !reflection_plan.has_value()) {
      apply_reflection_failure(
          result,
          contracts::ResultCode::PolicyDenied,
          make_error_info(contracts::ResultCode::PolicyDenied,
                          "cognition.reflection.policy",
                          "runtime policy snapshot could not produce a reflection stage plan",
                          "cognition::policy::StagePolicyResolver"),
          "reflection_pipeline.policy_projection_failed");
      return result;
    }

    const auto* reflection_hint = reflection_plan.has_value()
                                      ? find_stage_model_hint(
                                            *reflection_plan, "reflection", "failure_analysis")
                                      : nullptr;
    if (reflection_plan.has_value() && reflection_hint == nullptr) {
      apply_reflection_failure(
          result,
          contracts::ResultCode::PolicyDenied,
          make_error_info(contracts::ResultCode::PolicyDenied,
                          "cognition.reflection.policy",
                          "runtime policy snapshot did not expose the reflection bridge hint",
                          "cognition::policy::StagePolicyResolver"),
          "reflection_pipeline.policy_hints_missing");
      return result;
    }

    consume_reflection_bridge_stage(request, result, reflection_hint);
    if (result.error_info.has_value()) {
      return result;
    }

    ReflectionAnalysisRequest analysis_request;
    analysis_request.caller_domain = request.caller_domain;
    analysis_request.request_id = request.request_id;
    analysis_request.trace_id = request.trace_id;
    analysis_request.profile_id = request.profile_id;
    analysis_request.latest_observation = request.latest_observation;
    analysis_request.goal_contract = request.goal_contract;
    analysis_request.belief_state = request.belief_state;
    analysis_request.error_info = request.latest_observation.error;
    analysis_request.active_plan = std::nullopt;
    analysis_request.execution_hints = request.execution_hints;

    const auto reflection_decision = reflection_engine_.analyze(analysis_request);
    result.reflection_decision = reflection_decision;
    result.belief_update_hint = belief_update_synthesizer_.synthesize_from_reflection(
        reflection_decision, request.belief_state, request.latest_observation);
    append_unique(result.diagnostics, "reflection_pipeline.completed");
    return result;
  }

  void consume_decision_bridge_stage(const CognitionStepRequest& request,
                                     const std::string& stage,
                                     const std::string& task_type,
                                     ModelCapabilityTier capability_tier,
                                     bool requires_structured_output,
                                     std::uint32_t max_output_tokens,
                                     CognitionDecisionResult& result,
                                     const StageModelHint* stage_model_hint) const {
    if (!llm_bridge_) {
      append_unique(result.diagnostics, std::string{"llm_bridge.unavailable:"} + stage);
      return;
    }

    llm_bridge::StageLlmCallRequest bridge_request;
    bridge_request.request_id = request.request_id;
    bridge_request.trace_id = request.trace_id;
    bridge_request.llm_call_id = request.request_id + ":" + stage + ":" + task_type;
    bridge_request.stage_name = stage;
    bridge_request.task_type = task_type;
    bridge_request.messages = make_decision_stage_messages(request, stage, task_type);
    bridge_request.model_hint = stage_model_hint != nullptr
                    ? *stage_model_hint
                    : make_bridge_model_hint(
                        stage,
                        task_type,
                        capability_tier,
                        requires_structured_output,
                        max_output_tokens,
                        request.execution_hints.low_latency_preferred ? 1000U
                                              : 2500U);
    bridge_request.budget_context = request.budget_context;
    bridge_request.schema_spec = llm_bridge::StageSchemaSpec{
        .schema_kind = requires_structured_output ? llm_bridge::StageSchemaKind::JsonObject
                                                  : llm_bridge::StageSchemaKind::Text,
        .output_schema_ref = requires_structured_output
                                 ? std::string{"schema://cognition/"} + stage + "/" + task_type
                                 : std::string{},
        .allow_plain_text_fallback = !requires_structured_output,
    };

    const auto bridge_result = llm_bridge_->invoke_stage(bridge_request);
    append_bridge_diagnostics(result.diagnostics, bridge_result, stage);
    if (!bridge_result.error_info.has_value()) {
      return;
    }

    if (request.execution_hints.degraded_path_allowed) {
      append_unique(result.diagnostics, std::string{"decision_pipeline.llm_bridge_degraded:"} + stage);
      return;
    }

    apply_decision_failure(result,
                           bridge_result.result_code.value_or(
                               contracts::ResultCode::RuntimeRetryExhausted),
                           *bridge_result.error_info,
                           std::string{"decision_pipeline.llm_bridge_failed:"} + stage);
  }

  void consume_reflection_bridge_stage(const ReflectionRequest& request,
                                       CognitionReflectionResult& result,
                                       const StageModelHint* stage_model_hint) const {
    if (!llm_bridge_) {
      append_unique(result.diagnostics, "llm_bridge.unavailable:reflection");
      return;
    }

    llm_bridge::StageLlmCallRequest bridge_request;
    bridge_request.request_id = request.request_id;
    bridge_request.trace_id = request.trace_id;
    bridge_request.llm_call_id = request.request_id + ":reflection:failure_analysis";
    bridge_request.stage_name = "reflection";
    bridge_request.task_type = "failure_analysis";
    bridge_request.messages = make_reflection_stage_messages(request);
    bridge_request.model_hint = stage_model_hint != nullptr
                    ? *stage_model_hint
                    : make_bridge_model_hint(
                        "reflection",
                        "failure_analysis",
                        ModelCapabilityTier::Advanced,
                        true,
                        384U,
                        request.execution_hints.low_latency_preferred ? 1000U
                                              : 2500U);
    bridge_request.schema_spec = llm_bridge::StageSchemaSpec{
        .schema_kind = llm_bridge::StageSchemaKind::JsonObject,
        .output_schema_ref = "schema://cognition/reflection/failure_analysis",
        .allow_plain_text_fallback = false,
    };

    const auto bridge_result = llm_bridge_->invoke_stage(bridge_request);
    append_bridge_diagnostics(result.diagnostics, bridge_result, "reflection");
    if (!bridge_result.error_info.has_value()) {
      return;
    }

    if (request.execution_hints.degraded_path_allowed) {
      append_unique(result.diagnostics, "reflection_pipeline.llm_bridge_degraded:reflection");
      return;
    }

    apply_reflection_failure(result,
                             bridge_result.result_code.value_or(
                                 contracts::ResultCode::RuntimeRetryExhausted),
                             *bridge_result.error_info,
                             "reflection_pipeline.llm_bridge_failed:reflection");
  }

  CognitionConfig config_;
  perception::PerceptionEngine perception_engine_;
  planning::Planner planner_;
  reasoning::Reasoner reasoner_;
  reflection::ReflectionEngine reflection_engine_;
  belief::BeliefUpdateSynthesizer belief_update_synthesizer_;
  validation::StageOutputValidator validator_;
  observability::CognitionTelemetry telemetry_;
  std::shared_ptr<CognitionLlmBridge> llm_bridge_;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot_;
};

}  // namespace

std::unique_ptr<ICognitionEngine> create_cognition_engine(const CognitionConfig& config) {
  return std::make_unique<CognitionFacade>(config, CognitionRuntimeDependencies{});
}

std::unique_ptr<ICognitionEngine> create_cognition_engine(
    const CognitionConfig& config,
    CognitionRuntimeDependencies dependencies) {
  return std::make_unique<CognitionFacade>(config, std::move(dependencies));
}

std::unique_ptr<ICognitionEngine> create_cognition_engine(
    const profiles::RuntimePolicySnapshot& snapshot,
    CognitionRuntimeDependencies dependencies) {
  const auto config = config::CognitionConfigProjector::project_config(snapshot);
  if (!config.has_value()) {
    return nullptr;
  }

  if (dependencies.policy_snapshot == nullptr) {
    dependencies.policy_snapshot =
        std::make_shared<const profiles::RuntimePolicySnapshot>(snapshot);
  }
  return std::make_unique<CognitionFacade>(*config, std::move(dependencies));
}

}  // namespace dasall::cognition

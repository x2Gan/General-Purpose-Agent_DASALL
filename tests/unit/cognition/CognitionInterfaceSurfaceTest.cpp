#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "agent/GoalContract.h"
#include "checkpoint/ReflectionDecision.h"
#include "CognitionConfig.h"
#include "CognitionTypes.h"
#include "ICognitionEngine.h"
#include "IPlanner.h"
#include "IReasoner.h"
#include "IReflectionEngine.h"
#include "IResponseBuilder.h"
#include "belief/BeliefUpdateHint.h"
#include "decision/ActionDecision.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "perception/PerceptionResult.h"
#include "response/ResponseBuildRequest.h"
#include "response/ResponseBuildResult.h"
#include "support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct has_legacy_step_member : std::false_type {};

template <typename T>
struct has_legacy_step_member<T, std::void_t<decltype(&T::step)>> : std::true_type {};

template <typename T, typename = void>
struct has_agent_result_member : std::false_type {};

template <typename T>
struct has_agent_result_member<T, std::void_t<decltype(std::declval<T>().agent_result)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_reflection_decision_member : std::false_type {};

template <typename T>
struct has_reflection_decision_member<T,
                                      std::void_t<decltype(std::declval<T>().reflection_decision)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_recovery_request_member : std::false_type {};

template <typename T>
struct has_recovery_request_member<T,
                                   std::void_t<decltype(std::declval<T>().recovery_request)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_publish_channel_member : std::false_type {};

template <typename T>
struct has_publish_channel_member<T,
                                  std::void_t<decltype(std::declval<T>().publish_channel)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_target_platform_member : std::false_type {};

template <typename T>
struct has_target_platform_member<T,
                                  std::void_t<decltype(std::declval<T>().target_platform)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_tool_request_member : std::false_type {};

template <typename T>
struct has_tool_request_member<T, std::void_t<decltype(std::declval<T>().tool_request)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_retry_after_member : std::false_type {};

template <typename T>
struct has_retry_after_member<T, std::void_t<decltype(std::declval<T>().retry_after_ms)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_provider_payload_member : std::false_type {};

template <typename T>
struct has_provider_payload_member<T,
                                   std::void_t<decltype(std::declval<T>().provider_payload)>>
    : std::true_type {};

void test_cognition_unit_topology_names_are_specific() {
  using dasall::tests::support::assert_true;

  constexpr std::string_view ctest_name = "CognitionInterfaceSurfaceTest";
  constexpr std::string_view target_name = "dasall_cognition_interface_surface_unit_test";

  assert_true(ctest_name != "InterfaceSurfaceTest",
              "cognition unit topology should not reuse a generic InterfaceSurfaceTest name");
  assert_true(ctest_name.find("Cognition") == 0U,
              "cognition unit topology should keep a cognition-specific ctest prefix");
  assert_true(target_name.find("dasall_cognition_") == 0U,
              "cognition unit target should remain namespaced under dasall_cognition");
}

void test_cognition_config_defaults_match_profile_projection_table() {
  using dasall::cognition::CognitionConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(CognitionConfig{}.enabled), bool>);
  static_assert(std::is_same_v<decltype(CognitionConfig{}.max_plan_nodes), std::uint32_t>);
  static_assert(std::is_same_v<decltype(CognitionConfig{}.max_plan_depth), std::uint32_t>);
  static_assert(!has_target_platform_member<CognitionConfig>::value);
  static_assert(!has_provider_payload_member<CognitionConfig>::value);

  const CognitionConfig config;
  assert_true(config.enabled, "cognition should be enabled by default across profiles");
  assert_true(config.max_plan_nodes == 8U, "default max_plan_nodes should match cognition 6.10");
  assert_true(config.max_plan_depth == 4U, "default max_plan_depth should match cognition 6.10");
  assert_true(config.thresholds.ask_clarification == 0.45F,
              "default clarification threshold should match cognition 6.10");
  assert_true(config.thresholds.direct_response == 0.70F,
              "default direct response threshold should match cognition 6.10");
  assert_true(config.thresholds.replan_hint == 0.50F,
              "default replan threshold should match cognition 6.10");
  assert_true(config.perception.rule_fallback_enabled,
              "perception rule fallback should be enabled by default");
  assert_true(config.response.template_fallback_enabled,
              "response template fallback should be enabled by default");
  assert_true(!config.reasoner.allow_delegate_hint,
              "delegate hints should stay disabled by default");
  assert_true(config.observability.emit_stage_spans,
              "stage span emission should be enabled by default");
  assert_true(config.observability.redact_context_payload,
              "context payload redaction should be enabled by default");
}

void test_cognition_engine_surface_freezes_runtime_facing_entries() {
  using dasall::cognition::CognitionConfig;
  using dasall::cognition::CognitionDecisionResult;
  using dasall::cognition::CognitionReflectionResult;
  using dasall::cognition::CognitionStepRequest;
  using dasall::cognition::ICognitionEngine;
  using dasall::cognition::ReflectionRequest;
  using dasall::cognition::create_cognition_engine;
  using dasall::tests::support::assert_true;

  static_assert(std::is_abstract_v<ICognitionEngine>);
  static_assert(std::has_virtual_destructor_v<ICognitionEngine>);
  static_assert(std::is_same_v<decltype(&ICognitionEngine::decide),
                               CognitionDecisionResult (ICognitionEngine::*)(
                                   const CognitionStepRequest&)>);
  static_assert(std::is_same_v<decltype(&ICognitionEngine::reflect),
                               CognitionReflectionResult (ICognitionEngine::*)(
                                   const ReflectionRequest&)>);
  static_assert(std::is_same_v<decltype(&create_cognition_engine),
                               std::unique_ptr<ICognitionEngine> (*)(
                                   const CognitionConfig&)>);
  static_assert(!has_legacy_step_member<ICognitionEngine>::value);

  auto engine = create_cognition_engine(CognitionConfig{});
  assert_true(engine != nullptr, "cognition engine factory should return a usable interface");
}

void test_decide_and_reflect_request_result_family_freezes_fields() {
  using dasall::cognition::BudgetContext;
  using dasall::cognition::CognitionDecisionResult;
  using dasall::cognition::CognitionReflectionResult;
  using dasall::cognition::CognitionStepRequest;
  using dasall::cognition::ContextSufficiencySignal;
  using dasall::cognition::ReflectionRequest;
  using dasall::cognition::StageExecutionHints;
  using dasall::cognition::belief::BeliefUpdateHint;
  using dasall::cognition::decision::ActionDecision;
  using dasall::contracts::BeliefState;
  using dasall::contracts::ContextPacket;
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::GoalContract;
  using dasall::contracts::Observation;
  using dasall::contracts::ReflectionDecision;
  using dasall::contracts::ResultCode;

  static_assert(std::is_same_v<decltype(StageExecutionHints{}.low_latency_preferred), bool>);
  static_assert(std::is_same_v<decltype(StageExecutionHints{}.degraded_path_allowed), bool>);
  static_assert(std::is_same_v<decltype(StageExecutionHints{}.risk_tolerance), float>);
  static_assert(std::is_same_v<decltype(StageExecutionHints{}.profile_variant_hint),
                               std::optional<std::string>>);
  static_assert(!has_retry_after_member<StageExecutionHints>::value);

  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.caller_domain), std::string>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.request_id), std::string>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.trace_id), std::string>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.profile_id), std::string>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.goal_contract), GoalContract>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.context_packet), ContextPacket>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.belief_state), BeliefState>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.latest_observation),
                               std::optional<Observation>>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.budget_context),
                               std::optional<BudgetContext>>);
  static_assert(std::is_same_v<decltype(CognitionStepRequest{}.execution_hints),
                               StageExecutionHints>);
  static_assert(!has_agent_result_member<CognitionStepRequest>::value);
  static_assert(!has_recovery_request_member<CognitionStepRequest>::value);

  static_assert(std::is_same_v<decltype(CognitionDecisionResult{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(CognitionDecisionResult{}.action_decision),
                               std::optional<ActionDecision>>);
  static_assert(std::is_same_v<decltype(CognitionDecisionResult{}.belief_update_hint),
                               std::optional<BeliefUpdateHint>>);
  static_assert(std::is_same_v<decltype(CognitionDecisionResult{}.error_info),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(CognitionDecisionResult{}.context_sufficiency),
                               ContextSufficiencySignal>);
  static_assert(std::is_same_v<decltype(CognitionDecisionResult{}.diagnostics),
                               std::vector<std::string>>);
  static_assert(!has_agent_result_member<CognitionDecisionResult>::value);
  static_assert(!has_reflection_decision_member<CognitionDecisionResult>::value);

  static_assert(std::is_same_v<decltype(ReflectionRequest{}.caller_domain), std::string>);
  static_assert(std::is_same_v<decltype(ReflectionRequest{}.goal_contract), GoalContract>);
  static_assert(std::is_same_v<decltype(ReflectionRequest{}.context_packet), ContextPacket>);
  static_assert(std::is_same_v<decltype(ReflectionRequest{}.belief_state), BeliefState>);
  static_assert(std::is_same_v<decltype(ReflectionRequest{}.latest_observation), Observation>);
  static_assert(std::is_same_v<decltype(ReflectionRequest{}.active_plan_ref),
                               std::optional<std::string>>);
  static_assert(!has_recovery_request_member<ReflectionRequest>::value);

  static_assert(std::is_same_v<decltype(CognitionReflectionResult{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(CognitionReflectionResult{}.reflection_decision),
                               std::optional<ReflectionDecision>>);
  static_assert(std::is_same_v<decltype(CognitionReflectionResult{}.belief_update_hint),
                               std::optional<BeliefUpdateHint>>);
  static_assert(std::is_same_v<decltype(CognitionReflectionResult{}.error_info),
                               std::optional<ErrorInfo>>);
  static_assert(!has_agent_result_member<CognitionReflectionResult>::value);
  static_assert(!has_recovery_request_member<CognitionReflectionResult>::value);
}

void test_response_builder_surface_freezes_public_entry() {
  using dasall::cognition::CognitionConfig;
  using dasall::cognition::IResponseBuilder;
  using dasall::cognition::ResponseBuildRequest;
  using dasall::cognition::ResponseBuildResult;
  using dasall::cognition::create_response_builder;
  using dasall::tests::support::assert_true;

  static_assert(std::is_abstract_v<IResponseBuilder>);
  static_assert(std::has_virtual_destructor_v<IResponseBuilder>);
  static_assert(std::is_same_v<decltype(&IResponseBuilder::build),
                               ResponseBuildResult (IResponseBuilder::*)(
                                   const ResponseBuildRequest&)>);
  static_assert(std::is_same_v<decltype(&create_response_builder),
                               std::unique_ptr<IResponseBuilder> (*)(
                                   const CognitionConfig&)>);

  auto builder = create_response_builder(CognitionConfig{});
  assert_true(builder != nullptr, "response builder factory should return a usable interface");
}

void test_response_and_perception_object_headers_are_module_public() {
  using dasall::cognition::ResponseBuildHints;
  using dasall::cognition::ResponseBuildRequest;
  using dasall::cognition::ResponseBuildResult;
  using dasall::cognition::decision::ActionDecision;
  using dasall::cognition::perception::AmbiguityMarker;
  using dasall::cognition::perception::ClarificationCandidate;
  using dasall::cognition::perception::ConstraintDigest;
  using dasall::cognition::perception::EntityCandidate;
  using dasall::cognition::perception::PerceptionResult;
  using dasall::contracts::AgentResult;
  using dasall::contracts::BeliefState;
  using dasall::contracts::ContextPacket;
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::GoalContract;
  using dasall::contracts::Observation;
  using dasall::contracts::ResultCode;

  static_assert(std::is_same_v<decltype(ResponseBuildHints{}.prefer_template), bool>);
  static_assert(std::is_same_v<decltype(ResponseBuildHints{}.allow_template_fallback), bool>);
  static_assert(std::is_same_v<decltype(ResponseBuildHints{}.max_summary_chars), std::uint32_t>);
  static_assert(std::is_same_v<decltype(ResponseBuildHints{}.required_sections),
                               std::vector<std::string>>);

  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.caller_domain), std::string>);
  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.goal_contract), GoalContract>);
  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.context_packet), ContextPacket>);
  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.belief_state),
                               std::optional<BeliefState>>);
  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.latest_observation),
                               std::optional<Observation>>);
  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.terminal_decision),
                               std::optional<ActionDecision>>);
  static_assert(std::is_same_v<decltype(ResponseBuildRequest{}.build_hints),
                               ResponseBuildHints>);
  static_assert(!has_tool_request_member<ResponseBuildRequest>::value);
  static_assert(!has_publish_channel_member<ResponseBuildRequest>::value);

  static_assert(std::is_same_v<decltype(ResponseBuildResult{}.result_code),
                               std::optional<ResultCode>>);
  static_assert(std::is_same_v<decltype(ResponseBuildResult{}.agent_result),
                               std::optional<AgentResult>>);
  static_assert(std::is_same_v<decltype(ResponseBuildResult{}.error_info),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(ResponseBuildResult{}.fallback_used), bool>);
  static_assert(!has_tool_request_member<ResponseBuildResult>::value);
  static_assert(!has_publish_channel_member<ResponseBuildResult>::value);

  static_assert(std::is_same_v<decltype(EntityCandidate{}.name), std::string>);
  static_assert(std::is_same_v<decltype(EntityCandidate{}.evidence_refs),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ConstraintDigest{}.hard_constraints),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(AmbiguityMarker{}.missing_evidence_refs),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ClarificationCandidate{}.question), std::string>);
  static_assert(std::is_same_v<decltype(PerceptionResult{}.entities),
                               std::vector<EntityCandidate>>);
  static_assert(std::is_same_v<decltype(PerceptionResult{}.constraints_digest),
                               ConstraintDigest>);
  static_assert(std::is_same_v<decltype(PerceptionResult{}.requires_clarification), bool>);
  static_assert(!has_tool_request_member<PerceptionResult>::value);
  static_assert(!has_provider_payload_member<PerceptionResult>::value);
}

void test_stage_component_headers_are_markers_not_publicly_constructible_components() {
  using dasall::cognition::IPlanner;
  using dasall::cognition::IReasoner;
  using dasall::cognition::IReflectionEngine;

  static_assert(std::has_virtual_destructor_v<IPlanner>);
  static_assert(std::has_virtual_destructor_v<IReasoner>);
  static_assert(std::has_virtual_destructor_v<IReflectionEngine>);
  static_assert(!std::is_default_constructible_v<IPlanner>);
  static_assert(!std::is_default_constructible_v<IReasoner>);
  static_assert(!std::is_default_constructible_v<IReflectionEngine>);
}

void test_current_supporting_types_remain_module_public() {
  using dasall::cognition::belief::BeliefUpdateHint;
  using dasall::cognition::decision::ActionDecision;
  using dasall::cognition::decision::ActionDecisionKind;

  static_assert(std::is_same_v<decltype(ActionDecision{}.decision_kind),
                               ActionDecisionKind>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.merge_mode), std::string>);
}

}  // namespace

int main() {
  try {
    test_cognition_unit_topology_names_are_specific();
    test_cognition_config_defaults_match_profile_projection_table();
    test_cognition_engine_surface_freezes_runtime_facing_entries();
    test_decide_and_reflect_request_result_family_freezes_fields();
    test_response_builder_surface_freezes_public_entry();
    test_response_and_perception_object_headers_are_module_public();
    test_stage_component_headers_are_markers_not_publicly_constructible_components();
    test_current_supporting_types_remain_module_public();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

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
#include "../../../profiles/include/RuntimePolicySnapshot.h"
#include "belief/BeliefUpdateHint.h"
#include "decision/ActionDecision.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "perception/PerceptionResult.h"
#include "plan/PlanGraph.h"
#include "plan/ReplanResult.h"
#include "response/ResponseBuildRequest.h"
#include "response/ResponseBuildResult.h"
#include "support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct has_legacy_step_member : std::false_type {};

template <typename T>
struct has_legacy_step_member<T, std::void_t<decltype(&T::step)>> : std::true_type {};

template <typename T, typename = void>
struct has_init_member : std::false_type {};

template <typename T>
struct has_init_member<T, std::void_t<decltype(&T::init)>> : std::true_type {};

template <typename T, typename = void>
struct has_reflect_member : std::false_type {};

template <typename T>
struct has_reflect_member<T, std::void_t<decltype(&T::reflect)>> : std::true_type {};

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

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_interface_surface_snapshot() {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return RuntimePolicySnapshot{
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{.max_tokens = 2048U,
                                       .max_turns = 6U,
                                       .max_tool_calls = 2U,
                                       .max_latency_ms = 1800U,
                                       .max_replan_count = 2U},
      ModelProfile{.stage_routes = {
                       {"planning",
                        ModelRoutePolicy{.route = "desktop_full.planning.primary",
                                         .fallback_route = "desktop_full.planning.fallback",
                                         .streaming_enabled = false}},
                       {"execution",
                        ModelRoutePolicy{.route = "desktop_full.execution.primary",
                                         .fallback_route = "desktop_full.execution.fallback",
                                         .streaming_enabled = false}},
                       {"reflection",
                        ModelRoutePolicy{.route = "desktop_full.reflection.primary",
                                         .fallback_route = "desktop_full.reflection.fallback",
                                         .streaming_enabled = false}},
                       {"response",
                        ModelRoutePolicy{.route = "desktop_full.response.primary",
                                         .fallback_route = "desktop_full.response.fallback",
                                         .streaming_enabled = true}},
                   }},
      TokenBudgetPolicy{.max_input_tokens = 2048U,
                        .max_output_tokens = 512U,
                        .max_history_turns = 8U,
                        .compression_threshold = 1024U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 1000,
                            .expire_after_ms = 2000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 100},
      DegradePolicy{.fallback_chain = {"safe_mode"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 1800,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 2U},
                    .tool = TimeoutBudget{.timeout_ms = 1000,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 2U},
                    .mcp = TimeoutBudget{.timeout_ms = 1000,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 2U},
                    .workflow = TimeoutBudget{.timeout_ms = 1000,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 2U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "core",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = false,
                .upgrade_strategy = "rolling"},
      2U,
  };
}

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
struct has_tool_name_member : std::false_type {};

template <typename T>
struct has_tool_name_member<T, std::void_t<decltype(std::declval<T>().tool_name)>>
  : std::true_type {};

template <typename T, typename = void>
struct has_tool_arguments_payload_member : std::false_type {};

template <typename T>
struct has_tool_arguments_payload_member<T,
                    std::void_t<decltype(
                      std::declval<T>().tool_arguments_payload)>>
  : std::true_type {};

template <typename T, typename = void>
struct has_response_text_member : std::false_type {};

template <typename T>
struct has_response_text_member<T, std::void_t<decltype(std::declval<T>().response_text)>>
  : std::true_type {};

template <typename T, typename = void>
struct has_retry_after_member : std::false_type {};

template <typename T>
struct has_retry_after_member<T, std::void_t<decltype(std::declval<T>().retry_after_ms)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_deadline_ms_member : std::false_type {};

template <typename T>
struct has_deadline_ms_member<T, std::void_t<decltype(std::declval<T>().deadline_ms)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_lease_id_member : std::false_type {};

template <typename T>
struct has_lease_id_member<T, std::void_t<decltype(std::declval<T>().lease_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_worker_state_member : std::false_type {};

template <typename T>
struct has_worker_state_member<T, std::void_t<decltype(std::declval<T>().worker_state)>>
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
  using dasall::cognition::CognitionRuntimeDependencies;
  using dasall::cognition::CognitionDecisionResult;
  using dasall::cognition::CognitionReflectionResult;
  using dasall::cognition::CognitionStepRequest;
  using dasall::cognition::ICognitionEngine;
  using dasall::cognition::ReflectionRequest;
  using dasall::cognition::create_cognition_engine;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_abstract_v<ICognitionEngine>);
  static_assert(std::has_virtual_destructor_v<ICognitionEngine>);
  static_assert(std::is_same_v<decltype(&ICognitionEngine::decide),
                               CognitionDecisionResult (ICognitionEngine::*)(
                                   const CognitionStepRequest&)>);
  static_assert(std::is_same_v<decltype(&ICognitionEngine::reflect),
                               CognitionReflectionResult (ICognitionEngine::*)(
                                   const ReflectionRequest&)>);
  using EngineFactorySignature =
      std::unique_ptr<ICognitionEngine> (*)(const CognitionConfig&);
  using EngineDependencyFactorySignature =
      std::unique_ptr<ICognitionEngine> (*)(const CognitionConfig&,
                                            CognitionRuntimeDependencies);
    using EngineSnapshotFactorySignature =
      std::unique_ptr<ICognitionEngine> (*)(const RuntimePolicySnapshot&,
                        CognitionRuntimeDependencies);
  static_assert(std::is_same_v<decltype(static_cast<EngineFactorySignature>(
                                   &create_cognition_engine)),
                               EngineFactorySignature>);
  static_assert(std::is_same_v<decltype(static_cast<EngineDependencyFactorySignature>(
                                   &create_cognition_engine)),
                               EngineDependencyFactorySignature>);
    static_assert(std::is_same_v<decltype(static_cast<EngineSnapshotFactorySignature>(
                     &create_cognition_engine)),
                   EngineSnapshotFactorySignature>);
  static_assert(!has_legacy_step_member<ICognitionEngine>::value);
  static_assert(!has_init_member<ICognitionEngine>::value);

  auto engine = create_cognition_engine(CognitionConfig{});
  assert_true(engine != nullptr, "cognition engine factory should return a usable interface");
  auto engine_with_dependencies =
      create_cognition_engine(CognitionConfig{}, CognitionRuntimeDependencies{});
  assert_true(engine_with_dependencies != nullptr,
              "cognition engine factory should accept optional runtime dependencies");
    auto engine_from_snapshot =
      create_cognition_engine(make_interface_surface_snapshot(), CognitionRuntimeDependencies{});
    assert_true(engine_from_snapshot != nullptr,
          "cognition engine factory should accept runtime policy snapshot composition");
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
  static_assert(std::is_same_v<decltype(ReflectionRequest{}.active_plan),
                               std::optional<dasall::cognition::plan::PlanGraph>>);
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
  using dasall::cognition::CognitionRuntimeDependencies;
  using dasall::cognition::IResponseBuilder;
  using dasall::cognition::ResponseBuildRequest;
  using dasall::cognition::ResponseBuildResult;
  using dasall::cognition::create_response_builder;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_abstract_v<IResponseBuilder>);
  static_assert(std::has_virtual_destructor_v<IResponseBuilder>);
  static_assert(std::is_same_v<decltype(&IResponseBuilder::build),
                               ResponseBuildResult (IResponseBuilder::*)(
                                   const ResponseBuildRequest&)>);
  using ResponseBuilderFactorySignature =
      std::unique_ptr<IResponseBuilder> (*)(const CognitionConfig&);
  using ResponseBuilderDependencyFactorySignature =
      std::unique_ptr<IResponseBuilder> (*)(const CognitionConfig&,
                                            CognitionRuntimeDependencies);
    using ResponseBuilderSnapshotFactorySignature =
      std::unique_ptr<IResponseBuilder> (*)(const RuntimePolicySnapshot&,
                        CognitionRuntimeDependencies);
  static_assert(std::is_same_v<decltype(static_cast<ResponseBuilderFactorySignature>(
                                   &create_response_builder)),
                               ResponseBuilderFactorySignature>);
  static_assert(std::is_same_v<decltype(static_cast<ResponseBuilderDependencyFactorySignature>(
                                   &create_response_builder)),
                               ResponseBuilderDependencyFactorySignature>);
    static_assert(std::is_same_v<decltype(static_cast<ResponseBuilderSnapshotFactorySignature>(
                     &create_response_builder)),
                   ResponseBuilderSnapshotFactorySignature>);

  auto builder = create_response_builder(CognitionConfig{});
  assert_true(builder != nullptr, "response builder factory should return a usable interface");
  auto builder_with_dependencies =
      create_response_builder(CognitionConfig{}, CognitionRuntimeDependencies{});
  assert_true(builder_with_dependencies != nullptr,
              "response builder factory should accept optional runtime dependencies");
    auto builder_from_snapshot =
      create_response_builder(make_interface_surface_snapshot(), CognitionRuntimeDependencies{});
    assert_true(builder_from_snapshot != nullptr,
          "response builder factory should accept runtime policy snapshot composition");
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

void test_plan_graph_and_replan_object_headers_are_module_public() {
  using dasall::cognition::plan::PlanEdge;
  using dasall::cognition::plan::PlanGraph;
  using dasall::cognition::plan::PlanNode;
  using dasall::cognition::plan::PlanOpenQuestion;
  using dasall::cognition::plan::ReplanResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PlanNode{}.node_id), std::string>);
  static_assert(std::is_same_v<decltype(PlanNode{}.objective), std::string>);
  static_assert(std::is_same_v<decltype(PlanNode{}.success_signal), std::string>);
  static_assert(std::is_same_v<decltype(PlanNode{}.action_kind_hint), std::string>);
  static_assert(std::is_same_v<decltype(PlanNode{}.depends_on),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(PlanNode{}.evidence_refs),
                               std::vector<std::string>>);
  static_assert(!has_deadline_ms_member<PlanNode>::value);
  static_assert(!has_lease_id_member<PlanNode>::value);
  static_assert(!has_worker_state_member<PlanNode>::value);

  static_assert(std::is_same_v<decltype(PlanEdge{}.from_node_id), std::string>);
  static_assert(std::is_same_v<decltype(PlanEdge{}.to_node_id), std::string>);
  static_assert(std::is_same_v<decltype(PlanEdge{}.condition), std::string>);
  static_assert(std::is_same_v<decltype(PlanEdge{}.evidence_refs),
                               std::vector<std::string>>);
  static_assert(!has_retry_after_member<PlanEdge>::value);

  static_assert(std::is_same_v<decltype(PlanOpenQuestion{}.question_id), std::string>);
  static_assert(std::is_same_v<decltype(PlanOpenQuestion{}.question), std::string>);
  static_assert(std::is_same_v<decltype(PlanOpenQuestion{}.reason), std::string>);
  static_assert(std::is_same_v<decltype(PlanOpenQuestion{}.blocks_plan), bool>);
  static_assert(std::is_same_v<decltype(PlanOpenQuestion{}.evidence_refs),
                               std::vector<std::string>>);
  static_assert(!has_publish_channel_member<PlanOpenQuestion>::value);

  static_assert(std::is_same_v<decltype(PlanGraph{}.plan_id), std::string>);
  static_assert(std::is_same_v<decltype(PlanGraph{}.revision), std::uint32_t>);
  static_assert(std::is_same_v<decltype(PlanGraph{}.nodes), std::vector<PlanNode>>);
  static_assert(std::is_same_v<decltype(PlanGraph{}.edges), std::vector<PlanEdge>>);
  static_assert(std::is_same_v<decltype(PlanGraph{}.open_questions),
                               std::vector<PlanOpenQuestion>>);
  static_assert(std::is_same_v<decltype(PlanGraph{}.plan_rationale), std::string>);
  static_assert(std::is_same_v<decltype(PlanGraph{}.estimated_complexity), std::uint32_t>);
  static_assert(!has_tool_request_member<PlanGraph>::value);
  static_assert(!has_recovery_request_member<PlanGraph>::value);
  static_assert(!has_provider_payload_member<PlanGraph>::value);

  static_assert(std::is_same_v<decltype(ReplanResult{}.new_plan), PlanGraph>);
  static_assert(std::is_same_v<decltype(ReplanResult{}.replaced_node_ids),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ReplanResult{}.replan_reason), std::string>);
  static_assert(std::is_same_v<decltype(ReplanResult{}.confidence), float>);
  static_assert(!has_retry_after_member<ReplanResult>::value);
  static_assert(!has_recovery_request_member<ReplanResult>::value);

  const PlanGraph default_plan;
  assert_true(default_plan.revision == 0U, "plan revision should start at zero");
  assert_true(default_plan.estimated_complexity == 0U,
              "plan complexity should default to unknown/zero");
  const PlanOpenQuestion default_question;
  assert_true(default_question.blocks_plan,
              "open questions should conservatively block plan execution by default");
}

void test_stage_component_headers_freeze_public_interfaces() {
  using dasall::cognition::BudgetContext;
  using dasall::cognition::IPlanner;
  using dasall::cognition::IReasoner;
  using dasall::cognition::IReflectionEngine;
  using dasall::cognition::PlanningRequest;
  using dasall::cognition::ReasoningRequest;
  using dasall::cognition::ReflectionAnalysisRequest;
  using dasall::cognition::ReplanRequest;
  using dasall::cognition::StageExecutionHints;
  using dasall::cognition::perception::PerceptionResult;
  using dasall::cognition::plan::PlanGraph;
  using dasall::cognition::plan::ReplanResult;
  using dasall::contracts::BeliefState;
  using dasall::contracts::ContextPacket;
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::GoalContract;
  using dasall::contracts::Observation;
  using dasall::contracts::ReflectionDecision;

  static_assert(std::is_abstract_v<IPlanner>);
  static_assert(std::is_abstract_v<IReasoner>);
  static_assert(std::is_abstract_v<IReflectionEngine>);
  static_assert(std::has_virtual_destructor_v<IPlanner>);
  static_assert(std::has_virtual_destructor_v<IReasoner>);
  static_assert(std::has_virtual_destructor_v<IReflectionEngine>);
  static_assert(std::is_same_v<decltype(&IPlanner::build_plan),
                               PlanGraph (IPlanner::*)(const PlanningRequest&)>);
  static_assert(std::is_same_v<decltype(&IPlanner::replan),
                               ReplanResult (IPlanner::*)(const ReplanRequest&)>);
  static_assert(std::is_same_v<decltype(&IReasoner::decide),
                               dasall::cognition::decision::ActionDecision (
                                   IReasoner::*)(const ReasoningRequest&)>);
  static_assert(std::is_same_v<decltype(&IReflectionEngine::analyze),
                               ReflectionDecision (IReflectionEngine::*)(
                                   const ReflectionAnalysisRequest&)>);
  static_assert(!std::is_default_constructible_v<IPlanner>);
  static_assert(!std::is_default_constructible_v<IReasoner>);
  static_assert(!std::is_default_constructible_v<IReflectionEngine>);
  static_assert(!has_reflect_member<IReflectionEngine>::value);

  static_assert(std::is_same_v<decltype(PlanningRequest{}.caller_domain), std::string>);
  static_assert(std::is_same_v<decltype(PlanningRequest{}.goal_contract), GoalContract>);
  static_assert(std::is_same_v<decltype(PlanningRequest{}.context_packet), ContextPacket>);
  static_assert(std::is_same_v<decltype(PlanningRequest{}.belief_state), BeliefState>);
  static_assert(std::is_same_v<decltype(PlanningRequest{}.perception_result),
                               PerceptionResult>);
  static_assert(std::is_same_v<decltype(PlanningRequest{}.budget_context),
                               std::optional<BudgetContext>>);
  static_assert(std::is_same_v<decltype(PlanningRequest{}.execution_hints),
                               StageExecutionHints>);
  static_assert(!has_recovery_request_member<PlanningRequest>::value);

  static_assert(std::is_same_v<decltype(ReplanRequest{}.caller_domain), std::string>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.goal_contract), GoalContract>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.context_packet), ContextPacket>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.belief_state), BeliefState>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.active_plan), PlanGraph>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.latest_observation), Observation>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.budget_context),
                               std::optional<BudgetContext>>);
  static_assert(std::is_same_v<decltype(ReplanRequest{}.execution_hints),
                               StageExecutionHints>);
  static_assert(!has_recovery_request_member<ReplanRequest>::value);

  static_assert(std::is_same_v<decltype(ReasoningRequest{}.caller_domain), std::string>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.goal_contract), GoalContract>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.context_packet), ContextPacket>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.belief_state), BeliefState>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.perception_result),
                               PerceptionResult>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.active_plan), PlanGraph>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.latest_observation),
                               std::optional<Observation>>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.budget_context),
                               std::optional<BudgetContext>>);
  static_assert(std::is_same_v<decltype(ReasoningRequest{}.execution_hints),
                               StageExecutionHints>);
  static_assert(!has_tool_request_member<ReasoningRequest>::value);

  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.caller_domain),
                               std::string>);
  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.goal_contract),
                               GoalContract>);
  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.belief_state),
                               BeliefState>);
  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.latest_observation),
                               Observation>);
  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.error_info),
                               std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.active_plan),
                               std::optional<PlanGraph>>);
  static_assert(std::is_same_v<decltype(ReflectionAnalysisRequest{}.execution_hints),
                               StageExecutionHints>);
  static_assert(!has_recovery_request_member<ReflectionAnalysisRequest>::value);
}

void test_current_supporting_types_remain_module_public() {
  using dasall::cognition::BudgetContext;
  using dasall::cognition::ContextSufficiencySignal;
  using dasall::cognition::ModelCapabilityTier;
  using dasall::cognition::StageModelHint;
  using dasall::cognition::belief::AssumptionDelta;
  using dasall::cognition::belief::BeliefDeltaKind;
  using dasall::cognition::belief::BeliefMergeMode;
  using dasall::cognition::belief::BeliefUpdateHint;
  using dasall::cognition::belief::EvidenceRefDelta;
  using dasall::cognition::belief::FactDelta;
  using dasall::cognition::belief::HypothesisDelta;
  using dasall::cognition::decision::ActionDecision;
  using dasall::cognition::decision::ActionDecisionKind;
  using dasall::cognition::decision::CandidateDecisionScore;
  using dasall::cognition::decision::DelegateHint;
  using dasall::cognition::decision::ResponseOutline;
  using dasall::cognition::decision::ToolIntentHint;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ActionDecision{}.decision_kind),
                               ActionDecisionKind>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.selected_node_id),
                               std::optional<std::string>>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.rationale),
                               std::optional<std::string>>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.confidence), float>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.clarification_needed), bool>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.clarification_question),
                               std::optional<std::string>>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.tool_intent_hint),
                               std::optional<ToolIntentHint>>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.delegate_hint),
                               std::optional<DelegateHint>>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.response_outline),
                               std::optional<ResponseOutline>>);
  static_assert(std::is_same_v<decltype(ActionDecision{}.candidate_scores),
                               std::vector<CandidateDecisionScore>>);
  static_assert(!has_tool_name_member<ActionDecision>::value);
  static_assert(!has_tool_arguments_payload_member<ActionDecision>::value);
  static_assert(!has_response_text_member<ActionDecision>::value);

  static_assert(std::is_same_v<decltype(ToolIntentHint{}.tool_name), std::string>);
  static_assert(std::is_same_v<decltype(ToolIntentHint{}.intent_summary), std::string>);
  static_assert(std::is_same_v<decltype(ToolIntentHint{}.argument_hints),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ToolIntentHint{}.evidence_refs),
                               std::vector<std::string>>);

  static_assert(std::is_same_v<decltype(DelegateHint{}.delegate_target), std::string>);
  static_assert(std::is_same_v<decltype(DelegateHint{}.rationale), std::string>);
  static_assert(std::is_same_v<decltype(DelegateHint{}.confidence), float>);

  static_assert(std::is_same_v<decltype(ResponseOutline{}.summary), std::string>);
  static_assert(std::is_same_v<decltype(ResponseOutline{}.key_points),
                               std::vector<std::string>>);

  static_assert(std::is_same_v<decltype(CandidateDecisionScore{}.candidate_name),
                               std::string>);
  static_assert(std::is_same_v<decltype(CandidateDecisionScore{}.score), float>);
  static_assert(std::is_same_v<decltype(CandidateDecisionScore{}.rationale),
                               std::optional<std::string>>);

  static_assert(std::is_same_v<decltype(FactDelta{}.fact), std::string>);
  static_assert(std::is_same_v<decltype(FactDelta{}.delta_kind), BeliefDeltaKind>);
  static_assert(std::is_same_v<decltype(HypothesisDelta{}.hypothesis), std::string>);
  static_assert(std::is_same_v<decltype(HypothesisDelta{}.delta_kind), BeliefDeltaKind>);
  static_assert(std::is_same_v<decltype(AssumptionDelta{}.assumption), std::string>);
  static_assert(std::is_same_v<decltype(AssumptionDelta{}.delta_kind), BeliefDeltaKind>);
  static_assert(std::is_same_v<decltype(EvidenceRefDelta{}.evidence_ref), std::string>);
  static_assert(std::is_same_v<decltype(EvidenceRefDelta{}.delta_kind), BeliefDeltaKind>);

  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.confirmed_facts_delta),
                               std::vector<FactDelta>>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.hypotheses_delta),
                               std::vector<HypothesisDelta>>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.assumptions_delta),
                               std::vector<AssumptionDelta>>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.evidence_refs_delta),
                               std::vector<EvidenceRefDelta>>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.missing_evidence_refs),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.confidence_hint),
                               std::optional<float>>);
  static_assert(std::is_same_v<decltype(BeliefUpdateHint{}.merge_mode), BeliefMergeMode>);

  static_assert(std::is_same_v<decltype(StageModelHint{}.stage_name), std::string>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.task_type), std::string>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.capability_tier),
                               ModelCapabilityTier>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.max_output_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.deadline_ms), std::uint32_t>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.requires_structured_output), bool>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.requires_reasoning_trace), bool>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.cost_sensitivity), float>);
  static_assert(std::is_same_v<decltype(StageModelHint{}.preferred_provider), std::string>);

  static_assert(std::is_same_v<decltype(BudgetContext{}.total_budget_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(BudgetContext{}.consumed_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(BudgetContext{}.remaining_tokens), std::uint32_t>);
  static_assert(std::is_same_v<decltype(BudgetContext{}.budget_utilization), float>);
  static_assert(std::is_same_v<decltype(BudgetContext{}.context_was_truncated), bool>);
  static_assert(std::is_same_v<decltype(BudgetContext{}.near_budget_limit), bool>);

  static_assert(std::is_same_v<decltype(ContextSufficiencySignal{}.context_sufficient), bool>);
  static_assert(std::is_same_v<decltype(ContextSufficiencySignal{}.context_confidence), float>);
  static_assert(std::is_same_v<decltype(ContextSufficiencySignal{}.missing_evidence_hints),
                               std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(ContextSufficiencySignal{}.recommend_context_reload),
                               bool>);

  const StageModelHint default_hint;
  assert_true(default_hint.preferred_provider.empty(),
              "stage model hints should default to router-selected providers");
  const ContextSufficiencySignal default_signal;
  assert_true(default_signal.context_sufficient,
              "context sufficiency defaults should stay optimistic until evaluated");
  assert_true(!default_signal.recommend_context_reload,
              "context reload should require an explicit cognition recommendation");
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
    test_plan_graph_and_replan_object_headers_are_module_public();
    test_stage_component_headers_freeze_public_interfaces();
    test_current_supporting_types_remain_module_public();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

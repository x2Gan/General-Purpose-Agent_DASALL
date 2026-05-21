#include "AgentOrchestrator.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CognitionTypes.h"
#include "ICognitionEngine.h"
#include "IKnowledgeService.h"
#include "ILLMManager.h"
#include "IMemoryManager.h"
#include "IResponseBuilder.h"
#include "LLMGenerateRequest.h"
#include "RuntimeDependencySet.h"
#include "IToolManager.h"
#include "agent/GoalContract.h"
#include "checkpoint/CheckpointBuildTypes.h"
#include "checkpoint/RecoveryRequest.h"
#include "checkpoint/ReflectionDecision.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "fsm/AgentFsm.h"
#include "observation/Observation.h"

namespace dasall::runtime {
namespace {

constexpr std::int32_t kRuntimeOrchestratorSkeletonCompletedCode = 5002;
constexpr std::int32_t kRuntimeOrchestratorSkeletonFailedSafeCode = 5003;
constexpr std::int32_t kRuntimeOrchestratorSkeletonPreflightRejectedCode = 5004;
constexpr std::int32_t kRuntimeOrchestratorSkeletonInternalErrorCode = 5005;
constexpr std::int32_t kRuntimeOrchestratorWaitingCode = 5006;
constexpr std::int32_t kRuntimeOrchestratorLiveUnaryFailedCode = 5007;
constexpr std::int32_t kRuntimeOrchestratorDegradedCode = 5008;
constexpr std::int32_t kRuntimeOrchestratorSafeModeCode = 5009;

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::string make_result_id(const RuntimeState final_state) {
  return std::string{"rt-orchestrator-"} + runtime_state_name(final_state) + "-" +
         std::to_string(current_time_ms());
}

[[nodiscard]] std::uint32_t budget_value(const std::optional<std::uint32_t>& value,
                                         const std::uint32_t fallback) {
  return value.value_or(fallback);
}

[[nodiscard]] bool has_live_unary_ports(const OrchestratorComposition& composition) {
  return composition.dependency_set != nullptr &&
         composition.dependency_set->has_live_unary_ports();
}

[[nodiscard]] std::string make_tool_call_id(const contracts::AgentRequest& request) {
  return std::string{"tool-call-"} +
         request.request_id.value_or(std::string{"req-live-unary"});
}

[[nodiscard]] std::string make_recovery_binding_token(
    const contracts::AgentRequest& request,
    const contracts::Checkpoint& checkpoint) {
  return make_resume_binding_token(
      request.session_id.value_or(std::string{"session-live-unary"}),
      checkpoint.checkpoint_id.value_or(std::string{"chk-live-unary"}));
}

[[nodiscard]] std::uint32_t pending_interaction_timeout_ms(
    const OrchestratorComposition& composition) {
  if (composition.policy_snapshot != nullptr) {
    return static_cast<std::uint32_t>(
        composition.policy_snapshot->timeout_policy().workflow.timeout_ms);
  }

  return 60000U;
}

[[nodiscard]] bool has_production_llm_direct_path(
    const OrchestratorComposition& composition) {
  if (composition.dependency_set == nullptr ||
      composition.dependency_set->llm_manager == nullptr ||
      composition.policy_snapshot == nullptr) {
    return false;
  }

  const auto& evidence = composition.dependency_set->external_evidence;
  return std::any_of(evidence.begin(), evidence.end(), [](const auto& value) {
    return value.find("required-live-baseline") != std::string::npos;
  });
}

[[nodiscard]] std::optional<std::string> model_route_for_stage(
    const OrchestratorComposition& composition,
    const std::string& stage) {
  if (composition.policy_snapshot == nullptr) {
    return std::nullopt;
  }

  const auto& stage_routes = composition.policy_snapshot->model_profile().stage_routes;
  const auto route_it = stage_routes.find(stage);
  if (route_it == stage_routes.end() || route_it->second.route.empty()) {
    return std::nullopt;
  }

  return route_it->second.route;
}

[[nodiscard]] std::string make_runtime_constraints_tag(
    const contracts::ContextPacket& context_packet) {
  std::string constraints =
      "installed package run must answer through ILLMManager and must not use agent.dataset";
  if (context_packet.summary_memory.has_value() && !context_packet.summary_memory->empty()) {
    std::string prompt_visible_summary = *context_packet.summary_memory;
    const auto body_offset = prompt_visible_summary.find('\n');
    if (body_offset != std::string::npos && body_offset + 1U < prompt_visible_summary.size()) {
      prompt_visible_summary = prompt_visible_summary.substr(body_offset + 1U);
    }
    constraints += "; session_summary=" + prompt_visible_summary;
  }
  return constraints;
}

[[nodiscard]] llm::LLMGenerateRequest make_runtime_response_llm_request(
    const contracts::AgentRequest& request,
    const OrchestratorComposition& composition,
    const contracts::ContextPacket& context_packet,
    const contracts::RuntimeBudget& runtime_budget) {
  contracts::LLMRequest llm_request;
  llm_request.request_id = request.request_id;
  llm_request.llm_call_id = std::string("llm-call-") +
                            request.request_id.value_or(std::string{"req-live-unary"});
  llm_request.model_route = model_route_for_stage(composition, "response").value_or(
      model_route_for_stage(composition, "planning").value_or(std::string{"cloud.general"}));
  llm_request.request_mode = contracts::LLMRequestMode::Unary;
  llm_request.messages = std::vector<std::string>{request.user_input.value_or(std::string{})};
  llm_request.created_at = request.created_at.value_or(current_time_ms());
  llm_request.output_schema_ref = "schema://responder/default";
  llm_request.response_format = "text";
  llm_request.runtime_budget = runtime_budget;
  llm_request.max_output_tokens = budget_value(runtime_budget.max_tokens, 1024U);
  llm_request.timeout_ms = composition.policy_snapshot != nullptr
                               ? static_cast<std::uint32_t>(
                                     composition.policy_snapshot->timeout_policy().llm.timeout_ms)
                               : 30000U;
  llm_request.tags = std::vector<std::string>{
      "runtime",
      "production",
      std::string("user_goal=") + context_packet.current_goal_summary.value_or(
        request.user_input.value_or(std::string{})),
      std::string("constraints=") + make_runtime_constraints_tag(context_packet),
  };

  return llm::LLMGenerateRequest{
      .stage = "response",
      .task_type = "answer",
      .request = std::move(llm_request),
      .prompt_release_id_override = std::nullopt,
      .selection_hint = nullptr,
  };
}

[[nodiscard]] std::string make_llm_response_text(
    const llm::LLMManagerResult& result) {
  const auto& response = *result.response;
  std::string response_text = "llm.origin=" + result.resolved_route;
  if (response.model_name.has_value() && !response.model_name->empty()) {
    response_text += " model=" + *response.model_name;
  }
  if (response.finish_reason.has_value() && !response.finish_reason->empty()) {
    response_text += " finish_reason=" + *response.finish_reason;
  }
  response_text += "\n";
  response_text += response.content_payload.value_or(
      response.refusal_reason.value_or(std::string{"LLM returned an empty response"}));
  return response_text;
}

void append_unique_string(std::vector<std::string>& destination,
                          const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
    destination.push_back(value);
  }
}

void append_retrieval_evidence_ref(
    std::vector<contracts::RetrievalEvidenceRef>& destination,
    const contracts::RetrievalEvidenceRef& value) {
  if (!value.has_consistent_values()) {
    return;
  }

  const auto duplicate = std::find_if(destination.begin(), destination.end(),
                                      [&value](const auto& existing) {
                                        return existing.evidence_ref == value.evidence_ref;
                                      });
  if (duplicate == destination.end()) {
    destination.push_back(value);
  }
}

[[nodiscard]] knowledge::KnowledgeQuery make_knowledge_query(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const std::shared_ptr<const profiles::RuntimePolicySnapshot>& policy_snapshot) {
  knowledge::KnowledgeQuery query;
  query.request_id = request.request_id.value_or(std::string{"req-live-unary"});
  if (policy_snapshot != nullptr && !policy_snapshot->effective_profile_id().empty()) {
    query.profile_id = policy_snapshot->effective_profile_id();
  }
  query.session_id = request.session_id;
  query.goal_id = goal_id;
  query.query_text = request.user_input.value_or(goal_id);
  query.query_kind = knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  query.retrieval_evidence_budget_hint = 256U;
  return query;
}

[[nodiscard]] contracts::AgentRequest normalize_request(
    const contracts::AgentRequest& request,
    const std::uint64_t sequence) {
  contracts::AgentRequest normalized = request;
  if (!normalized.request_id.has_value() || normalized.request_id->empty()) {
    normalized.request_id = std::string{"req-"} + std::to_string(sequence);
  }
  if (!normalized.session_id.has_value() || normalized.session_id->empty()) {
    normalized.session_id = std::string{"session-"} + std::to_string(sequence);
  }
  if (!normalized.trace_id.has_value() || normalized.trace_id->empty()) {
    normalized.trace_id = std::string{"trace-"} + std::to_string(sequence);
  }
  if (!normalized.user_input.has_value()) {
    normalized.user_input = std::string{};
  }
  if (!normalized.request_channel.has_value()) {
    normalized.request_channel = contracts::RequestChannel::Cli;
  }
  if (!normalized.created_at.has_value()) {
    normalized.created_at = current_time_ms();
  }
  return normalized;
}

[[nodiscard]] contracts::RuntimeBudget effective_runtime_budget(
    const contracts::AgentRequest& request,
    const OrchestratorComposition& composition) {
  if (request.runtime_budget.has_value()) {
    return *request.runtime_budget;
  }

  return composition.default_runtime_budget;
}

[[nodiscard]] contracts::GoalContract make_goal_contract(
    const contracts::AgentRequest& request,
    const std::string& goal_id) {
  contracts::GoalContract goal_contract;
  goal_contract.goal_id = goal_id;
  goal_contract.request_id = request.request_id;
  goal_contract.goal_description = request.user_input.value_or(goal_id);
  goal_contract.success_criteria =
      "runtime live unary path materializes a tool observation and response";
  goal_contract.status = contracts::GoalStatus::Active;
  goal_contract.created_at = request.created_at.value_or(current_time_ms());
  goal_contract.tags = std::vector<std::string>{"runtime", "cognition", "true-integration"};
  return goal_contract;
}

[[nodiscard]] memory::MemoryContextRequest make_memory_context_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const OrchestratorComposition& composition,
    const contracts::RuntimeBudget& runtime_budget) {
  memory::MemoryContextRequest context_request;
  context_request.request_id = request.request_id.value_or(std::string{"req-live-unary"});
  context_request.session_id = request.session_id.value_or(std::string{"session-live-unary"});
  context_request.stage = "reasoning";
  context_request.goal_summary = request.user_input.value_or(goal_id);
  context_request.constraints_summary = composition.default_audit_summary;
  context_request.latest_observation_digest_summary = std::string{};
  context_request.visible_tools = composition.dependency_set->visible_tools;
  if (context_request.visible_tools.empty()) {
    context_request.visible_tools.push_back("agent.dataset");
  }
  context_request.token_budget_hint = static_cast<int>(
      budget_value(runtime_budget.max_tokens, 2048U));
  context_request.latency_budget_ms = static_cast<int>(
      budget_value(runtime_budget.max_latency_ms, 1500U));
  context_request.external_evidence = composition.dependency_set->external_evidence;
  if (composition.dependency_set->knowledge_service != nullptr) {
    const auto knowledge_result = composition.dependency_set->knowledge_service->retrieve(
        make_knowledge_query(request, goal_id, composition.policy_snapshot));
    if (knowledge_result.ok && knowledge_result.evidence.has_value()) {
      for (const auto& projection_line : knowledge_result.evidence->context_projection) {
        append_unique_string(context_request.external_evidence, projection_line);
      }
      for (const auto& ref : knowledge_result.retrieval_evidence_refs) {
        append_retrieval_evidence_ref(context_request.retrieval_evidence_refs, ref);
      }
    }
  }
  return context_request;
}

[[nodiscard]] std::optional<contracts::RuntimeBudget> runtime_budget_from_snapshot(
    const contracts::BudgetSnapshot& snapshot) {
  contracts::RuntimeBudget runtime_budget;
  bool has_token = false;
  bool has_turn = false;
  bool has_tool_call = false;
  bool has_latency = false;
  bool has_replan = false;

  for (const auto& entry : snapshot.entries) {
    switch (entry.budget_type) {
      case contracts::BudgetType::Token:
        runtime_budget.max_tokens = entry.max;
        has_token = true;
        break;
      case contracts::BudgetType::Turn:
        runtime_budget.max_turns = entry.max;
        has_turn = true;
        break;
      case contracts::BudgetType::ToolCall:
        runtime_budget.max_tool_calls = entry.max;
        has_tool_call = true;
        break;
      case contracts::BudgetType::Latency:
        runtime_budget.max_latency_ms = entry.max;
        has_latency = true;
        break;
      case contracts::BudgetType::Replan:
        runtime_budget.max_replan_count = entry.max;
        has_replan = true;
        break;
    }
  }

  if (!has_token || !has_turn || !has_tool_call || !has_latency || !has_replan) {
    return std::nullopt;
  }

  return runtime_budget;
}

[[nodiscard]] memory::MemoryContextRequest make_resume_memory_context_request(
    const contracts::AgentRequest& request,
    const ResumePlan& plan,
    const std::string& goal_id,
    const OrchestratorComposition& composition,
    const contracts::RuntimeBudget& runtime_budget) {
  auto context_request =
      make_memory_context_request(request, goal_id, composition, runtime_budget);
  context_request.stage = plan.target_state == RuntimeState::WaitingExternal
                              ? std::string{"reflection"}
                              : std::string{"reasoning"};
  context_request.latest_observation_digest_summary = plan.resume_reason.empty()
                                                          ? std::string{"resume checkpoint requested a fresh context packet"}
                                                          : plan.resume_reason;
  return context_request;
}

[[nodiscard]] std::string context_assembly_failure_detail(
    const memory::ContextAssemblyResult& context_result,
    const std::string& fallback_detail) {
  if (!context_result.warnings.empty()) {
    return context_result.warnings.front();
  }

  if (!context_result.compression_notes.empty()) {
    return context_result.compression_notes.front();
  }

  if (!context_result.dropped_sections.empty()) {
    return context_result.dropped_sections.front();
  }

  return fallback_detail;
}

[[nodiscard]] bool is_tolerable_live_context_warning(const std::string& warning) {
  return warning == "user_turn_fallback_goal_summary" ||
         warning == "goal_summary_fallback_working_memory";
}

[[nodiscard]] bool live_context_degradation_is_fatal(
    const memory::ContextAssemblyResult& context_result) {
  if (!context_result.degraded) {
    return false;
  }

  if (context_result.warnings.empty()) {
    return true;
  }

  return !std::all_of(context_result.warnings.begin(),
                      context_result.warnings.end(),
                      is_tolerable_live_context_warning);
}

[[nodiscard]] memory::ContextAssemblyResult prepare_resume_context(
    const OrchestratorComposition& composition,
    const contracts::AgentRequest& request,
    const ResumePlan& plan,
    const std::string& goal_id,
    const contracts::RuntimeBudget& runtime_budget) {
  if (composition.dependency_set == nullptr ||
      composition.dependency_set->memory_manager == nullptr) {
    memory::ContextAssemblyResult unavailable_result;
    unavailable_result.result_code = contracts::ResultCode::RuntimeRetryExhausted;
    unavailable_result.degraded = true;
    unavailable_result.warnings.push_back(
        "runtime resume path requires a memory manager to refresh context");
    return unavailable_result;
  }

  return composition.dependency_set->memory_manager->prepare_context(
      make_resume_memory_context_request(
          request,
          plan,
          goal_id,
          composition,
          runtime_budget));
}

[[nodiscard]] std::string join_strings(const std::vector<std::string>& values,
                                       const std::string& separator) {
  std::string joined;
  for (const auto& value : values) {
    if (value.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += separator;
    }
    joined += value;
  }
  return joined;
}

[[nodiscard]] bool has_confirmed_fact_deltas(
    const cognition::belief::BeliefUpdateHint& hint) {
  for (const auto& delta : hint.confirmed_facts_delta) {
    if (delta.delta_kind == cognition::belief::BeliefDeltaKind::Upsert && !delta.fact.empty()) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool should_write_back_belief_hint(
    const std::optional<cognition::belief::BeliefUpdateHint>& hint) {
  if (!hint.has_value()) {
    return false;
  }

  return has_confirmed_fact_deltas(*hint) || !hint->missing_evidence_refs.empty() ||
         !hint->hypotheses_delta.empty() || !hint->assumptions_delta.empty();
}

[[nodiscard]] std::uint32_t belief_confidence_score(
    const std::optional<float>& confidence_hint) {
  if (!confidence_hint.has_value()) {
    return 75U;
  }

  int score = static_cast<int>(*confidence_hint * 100.0F);
  if (*confidence_hint > 1.0F) {
    score = static_cast<int>(*confidence_hint);
  }
  if (score < 0) {
    score = 0;
  }
  if (score > 100) {
    score = 100;
  }
  return static_cast<std::uint32_t>(score);
}

[[nodiscard]] std::string belief_summary_text(
    const cognition::belief::BeliefUpdateHint& hint) {
  std::vector<std::string> summary_fragments;
  for (const auto& delta : hint.confirmed_facts_delta) {
    if (delta.delta_kind == cognition::belief::BeliefDeltaKind::Upsert && !delta.fact.empty()) {
      summary_fragments.push_back(delta.fact);
    }
  }
  if (!hint.missing_evidence_refs.empty()) {
    summary_fragments.push_back(
        std::string{"missing evidence refs: "} + join_strings(hint.missing_evidence_refs, ", "));
  }

  if (summary_fragments.empty()) {
    return "runtime projected cognition belief update";
  }

  return join_strings(summary_fragments, "; ");
}

[[nodiscard]] memory::MemoryWritebackRequest make_belief_writeback_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const cognition::belief::BeliefUpdateHint& hint,
    const cognition::CognitionDecisionResult& decision_result) {
  const auto request_id = request.request_id.value_or(std::string{"req-live-unary"});
  const auto session_id = request.session_id.value_or(std::string{"session-live-unary"});
  const auto created_at = request.created_at.value_or(current_time_ms());
  const auto summary_text = belief_summary_text(hint);
  const auto confidence_score = belief_confidence_score(hint.confidence_hint);

  memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = session_id;
  writeback_request.turn.turn_id = request_id + "-cognition-belief";
  writeback_request.turn.session_id = session_id;
  writeback_request.turn.user_input = request.user_input.value_or(goal_id);
  writeback_request.turn.agent_response = summary_text;
  writeback_request.turn.created_at = created_at;
  writeback_request.turn.tags = std::vector<std::string>{"runtime", "cognition", "belief_writeback"};

  writeback_request.summary_candidate = contracts::SummaryMemory{};
  writeback_request.summary_candidate->summary_text = summary_text;
  writeback_request.summary_candidate->source_turn_ids =
      std::vector<std::string>{*writeback_request.turn.turn_id};
  if (decision_result.action_decision.has_value() &&
      decision_result.action_decision->rationale.has_value() &&
      !decision_result.action_decision->rationale->empty()) {
    writeback_request.summary_candidate->decisions_made =
        std::vector<std::string>{*decision_result.action_decision->rationale};
  }

  std::vector<std::string> confirmed_facts;
  for (const auto& delta : hint.confirmed_facts_delta) {
    if (delta.delta_kind != cognition::belief::BeliefDeltaKind::Upsert || delta.fact.empty()) {
      continue;
    }

    confirmed_facts.push_back(delta.fact);
    memory::FactCandidate fact_candidate;
    fact_candidate.fact.fact_text = delta.fact;
    fact_candidate.fact.source_turn_ids =
        std::vector<std::string>{*writeback_request.turn.turn_id};
    fact_candidate.fact.confidence_score = confidence_score;
    fact_candidate.fact.created_at = created_at;
    fact_candidate.fact.fact_type = "cognition_belief";
    if (!hint.missing_evidence_refs.empty()) {
      fact_candidate.fact.tags = hint.missing_evidence_refs;
    }
    fact_candidate.extraction_source = "runtime.cognition.belief_hint";
    writeback_request.fact_candidates.push_back(std::move(fact_candidate));
  }

  if (!confirmed_facts.empty()) {
    writeback_request.summary_candidate->confirmed_facts = std::move(confirmed_facts);
  }

  return writeback_request;
}

[[nodiscard]] memory::MemoryWritebackRequest make_llm_direct_writeback_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const std::string& response_text) {
  const auto request_id = request.request_id.value_or(std::string{"req-live-unary"});
  const auto session_id = request.session_id.value_or(std::string{"session-live-unary"});
  const auto created_at = request.created_at.value_or(current_time_ms());

  memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = session_id;
  writeback_request.turn.turn_id = request_id + "-llm-response";
  writeback_request.turn.session_id = session_id;
  writeback_request.turn.user_input = request.user_input.value_or(goal_id);
  writeback_request.turn.agent_response = response_text;
  writeback_request.turn.created_at = created_at;
  writeback_request.turn.tags = std::vector<std::string>{"runtime", "llm", "direct_response"};

  writeback_request.summary_candidate = contracts::SummaryMemory{};
  writeback_request.summary_candidate->summary_text = response_text;
  writeback_request.summary_candidate->source_turn_ids =
      std::vector<std::string>{*writeback_request.turn.turn_id};
  writeback_request.summary_candidate->decisions_made =
      std::vector<std::string>{"production llm direct response completed"};
  writeback_request.summary_candidate->confirmed_facts =
      std::vector<std::string>{"llm direct response persisted through memory writeback"};
  return writeback_request;
}

[[nodiscard]] std::string make_context_reload_reason(
    const cognition::ContextSufficiencySignal& signal) {
  if (!signal.missing_evidence_hints.empty()) {
    return std::string{"cognition requested context reload for: "} +
           join_strings(signal.missing_evidence_hints, ", ");
  }

  return "cognition requested context reload before executing the action";
}

[[nodiscard]] memory::MemoryContextRequest make_context_reload_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const OrchestratorComposition& composition,
    const contracts::RuntimeBudget& runtime_budget,
    const cognition::ContextSufficiencySignal& signal) {
  auto context_request =
      make_memory_context_request(request, goal_id, composition, runtime_budget);
  context_request.stage = "reasoning_reload";
  context_request.latest_observation_digest_summary = make_context_reload_reason(signal);
  for (const auto& hint : signal.missing_evidence_hints) {
    if (!hint.empty()) {
      context_request.external_evidence.push_back(std::string{"cognition.missing_evidence:"} + hint);
    }
  }
  return context_request;
}

[[nodiscard]] bool should_consume_context_signal(
    const cognition::CognitionDecisionResult& decision_result) {
  return !decision_result.error_info.has_value() && !decision_result.result_code.has_value();
}

[[nodiscard]] bool should_degrade_to_waiting_clarify(
    const cognition::CognitionDecisionResult& decision_result) {
  if (!should_consume_context_signal(decision_result)) {
    return false;
  }

  if (decision_result.action_decision.has_value() &&
      decision_result.action_decision->clarification_needed) {
    return true;
  }

  return !decision_result.context_sufficiency.context_sufficient ||
         decision_result.context_sufficiency.recommend_context_reload ||
         !decision_result.context_sufficiency.missing_evidence_hints.empty();
}

[[nodiscard]] std::string clarification_response_text(
    const OrchestratorComposition& composition,
    const cognition::CognitionDecisionResult& decision_result) {
  if (decision_result.action_decision.has_value() &&
      decision_result.action_decision->clarification_question.has_value() &&
      !decision_result.action_decision->clarification_question->empty()) {
    return *decision_result.action_decision->clarification_question;
  }

  if (!decision_result.context_sufficiency.missing_evidence_hints.empty()) {
    return std::string{"need clarification before action execution: "} +
           join_strings(decision_result.context_sufficiency.missing_evidence_hints, ", ");
  }

  return composition.stub_ports.waiting_response_text;
}

[[nodiscard]] bool has_executable_action(
    const std::optional<cognition::decision::ActionDecision>& action_decision) {
  return action_decision.has_value() &&
         action_decision->decision_kind ==
             cognition::decision::ActionDecisionKind::ExecuteAction;
}

[[nodiscard]] bool has_terminal_response_action(
    const std::optional<cognition::decision::ActionDecision>& action_decision) {
  if (!action_decision.has_value()) {
    return false;
  }

  return action_decision->decision_kind ==
             cognition::decision::ActionDecisionKind::DirectResponse ||
         action_decision->decision_kind ==
             cognition::decision::ActionDecisionKind::ConvergeSafe;
}

[[nodiscard]] bool has_decision_error_action_conflict(
    const cognition::CognitionDecisionResult& decision_result) {
  return (has_executable_action(decision_result.action_decision) ||
          has_terminal_response_action(decision_result.action_decision)) &&
         (decision_result.error_info.has_value() || decision_result.result_code.has_value());
}

[[nodiscard]] contracts::ErrorInfo make_reflection_recovery_error(
    const contracts::Observation& latest_observation,
    const contracts::ReflectionDecision& reflection_decision) {
  const auto decision_kind = reflection_decision.decision_kind.value_or(
      contracts::ReflectionDecisionKind::AbortSafe);
  return contracts::ErrorInfo{
      .failure_type = contracts::ResultCodeCategory::Runtime,
      .retryable = decision_kind !=
                   contracts::ReflectionDecisionKind::AbortSafe,
      .safe_to_replan = decision_kind ==
                        contracts::ReflectionDecisionKind::Replan,
      .details = contracts::ErrorDetails{
          .code = kRuntimeOrchestratorLiveUnaryFailedCode,
          .message = reflection_decision.rationale.value_or(
              std::string{"reflection requested runtime recovery admission"}),
          .stage = std::string{"recovery_round"},
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = std::string{"tool_call"},
          .ref_id = latest_observation.tool_call_id.value_or(std::string{"tool-call-missing"}),
      },
  };
}

[[nodiscard]] contracts::Observation make_reflection_recovery_observation(
    const contracts::Observation& latest_observation,
    const contracts::ErrorInfo& error_info) {
  contracts::Observation failed_observation = latest_observation;
  if (!failed_observation.observation_id.has_value() ||
      failed_observation.observation_id->empty()) {
    failed_observation.observation_id = std::string{"observation:reflection-recovery"};
  }
  failed_observation.success = false;
  failed_observation.error = error_info;
  if (!failed_observation.source.has_value()) {
    failed_observation.source = contracts::ObservationSource::ToolExecution;
  }
  if (failed_observation.source == contracts::ObservationSource::ToolExecution) {
    failed_observation.worker_task_id = std::nullopt;
  }
  if (!failed_observation.created_at.has_value()) {
    failed_observation.created_at = current_time_ms();
  }
  if (!failed_observation.payload.has_value() || failed_observation.payload->empty()) {
    failed_observation.payload = std::string{"{}"};
  }
  if (failed_observation.duration_ms.has_value() && *failed_observation.duration_ms <= 0) {
    failed_observation.duration_ms = 1;
  }
  return failed_observation;
}

[[nodiscard]] contracts::IdempotencyAndSideEffectReport make_reflection_idempotency_report(
    const contracts::ToolRequest& tool_request,
    const tools::ToolInvocationEnvelope& tool_envelope) {
  const auto side_effects_present =
      tool_envelope.tool_result.has_value() &&
      tool_envelope.tool_result->side_effects.has_value() &&
      !tool_envelope.tool_result->side_effects->empty();

  contracts::IdempotencyAndSideEffectReport report;
  report.replay_safe = !side_effects_present;
  report.idempotency_key =
      tool_request.idempotency_key.has_value() && !tool_request.idempotency_key->empty()
          ? tool_request.idempotency_key
          : tool_request.tool_call_id;
  report.side_effects_present = side_effects_present;
  if (side_effects_present) {
    report.non_replayable_reason =
        "tool round observed side effects and cannot be replayed safely";
  }
  return report;
}

[[nodiscard]] contracts::RecoveryRequest make_reflection_recovery_request(
    const contracts::Checkpoint& checkpoint,
  const std::optional<contracts::BudgetSnapshot>& runtime_budget_snapshot,
    const contracts::Observation& latest_observation,
    const contracts::ReflectionDecision& reflection_decision,
    const contracts::ToolRequest& tool_request,
    const tools::ToolInvocationEnvelope& tool_envelope) {
  const auto error_info = make_reflection_recovery_error(latest_observation, reflection_decision);
  return contracts::RecoveryRequest{
      .reflection_decision = reflection_decision,
      .error_info = error_info,
      .latest_observation =
          make_reflection_recovery_observation(latest_observation, error_info),
      .checkpoint = checkpoint,
      .idempotency_and_side_effect_report =
          make_reflection_idempotency_report(tool_request, tool_envelope),
      .retry_count = checkpoint.retry_count,
        .runtime_budget_snapshot = runtime_budget_snapshot,
  };
}

[[nodiscard]] ResumePlan make_reflection_replan_resume_plan(
    const contracts::Checkpoint& checkpoint,
    const std::string& reason) {
  return ResumePlan{
      .checkpoint_ref = checkpoint.checkpoint_id.value_or(std::string{}),
      .target_state = RuntimeState::Planning,
      .checkpoint_state = checkpoint.state.value_or(contracts::CheckpointState::Unspecified),
      .resume_token = std::string{},
      .resume_reason = reason,
      .pending_action = checkpoint.pending_action,
      .policy_snapshot_ref = std::nullopt,
      .requires_operator_intervention = false,
  };
}

[[nodiscard]] cognition::CognitionStepRequest make_cognition_step_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const OrchestratorComposition& composition,
    const contracts::ContextPacket& context_packet,
    const contracts::RuntimeBudget& runtime_budget) {
  cognition::CognitionStepRequest cognition_request;
  cognition_request.caller_domain = "runtime.agent_orchestrator";
  cognition_request.request_id = request.request_id.value_or(std::string{"req-live-unary"});
  cognition_request.trace_id = request.trace_id.value_or(std::string{"trace-live-unary"});
  cognition_request.profile_id = composition.profile_id;
  cognition_request.goal_contract = make_goal_contract(request, goal_id);
  cognition_request.context_packet = context_packet;
  cognition_request.belief_state = contracts::BeliefState{
      .request_id = request.request_id,
      .confirmed_facts = std::vector<std::string>{"runtime live unary context prepared"},
      .hypotheses = std::vector<std::string>{"tool invocation will satisfy the unary request"},
      .assumptions = std::vector<std::string>{"visible tool surface is registered"},
      .evidence_refs = std::vector<std::string>{"runtime:memory_context"},
      .confidence = 0.75F,
      .goal_id = goal_id,
      .created_at = current_time_ms(),
      .tags = std::vector<std::string>{"runtime", "cognition", "true-integration"},
  };
  cognition_request.budget_context = cognition::BudgetContext{
      .total_budget_tokens = budget_value(runtime_budget.max_tokens, 2048U),
      .consumed_tokens = 0U,
      .remaining_tokens = budget_value(runtime_budget.max_tokens, 2048U),
      .budget_utilization = 0.0F,
      .context_was_truncated = false,
      .near_budget_limit = false,
  };
  return cognition_request;
}

[[nodiscard]] tools::ToolInvocationContext make_tool_invocation_context(
    const contracts::AgentRequest& request,
    const OrchestratorComposition& composition) {
  return tools::ToolInvocationContext{
      .caller_domain = std::string{"runtime.agent_orchestrator"},
      .session_id = request.session_id,
      .profile_snapshot = composition.policy_snapshot.get(),
      .trace = tools::ToolTraceContext{
          .trace_id = request.trace_id,
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

[[nodiscard]] contracts::ToolRequest make_tool_request(
    const contracts::AgentRequest& request,
    const contracts::RuntimeBudget& runtime_budget,
    const cognition::decision::ActionDecision& action_decision,
    const std::string& goal_id,
    const OrchestratorComposition& composition) {
  contracts::ToolRequest tool_request;
  const auto request_id = request.request_id.value_or(std::string{"req-live-unary"});
  const auto tool_call_id = make_tool_call_id(request);
  const auto* tool_hint = action_decision.tool_intent_hint.has_value()
                              ? &(*action_decision.tool_intent_hint)
                              : nullptr;
  tool_request.request_id = request.request_id;
  tool_request.tool_call_id = tool_call_id;
  if (tool_hint != nullptr && !tool_hint->tool_name.empty()) {
    tool_request.tool_name = tool_hint->tool_name;
  } else {
    tool_request.tool_name = std::string{"agent.dataset"};
  }
  tool_request.invocation_kind = contracts::ToolInvocationKind::InformationQuery;
  if (tool_hint != nullptr && !tool_hint->argument_hints.empty()) {
    tool_request.arguments_payload =
        std::string{"{\"query\":\""} + tool_hint->argument_hints.front() + "\"}";
  } else {
    tool_request.arguments_payload = std::string{"{}"};
  }
  tool_request.created_at = request.created_at.value_or(current_time_ms());
  tool_request.goal_id = goal_id;
  tool_request.worker_task_id = composition.default_worker_id;
  tool_request.runtime_budget = runtime_budget;
  if (composition.policy_snapshot != nullptr) {
    tool_request.timeout_ms = static_cast<std::uint32_t>(
        composition.policy_snapshot->timeout_policy().tool.timeout_ms);
  } else {
    tool_request.timeout_ms = 1000U;
  }
  tool_request.idempotency_key = tool_call_id;
  tool_request.tags = std::vector<std::string>{"runtime", "integration", "true-port"};
  return tool_request;
}

[[nodiscard]] cognition::ReflectionRequest make_reflection_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const OrchestratorComposition& composition,
    const contracts::ContextPacket& context_packet,
    const contracts::Observation& latest_observation) {
  cognition::ReflectionRequest reflection_request;
  reflection_request.caller_domain = "runtime.agent_orchestrator";
  reflection_request.request_id = request.request_id.value_or(std::string{"req-live-unary"});
  reflection_request.trace_id = request.trace_id.value_or(std::string{"trace-live-unary"});
  reflection_request.profile_id = composition.profile_id;
  reflection_request.goal_contract = make_goal_contract(request, goal_id);
  reflection_request.context_packet = context_packet;
  reflection_request.belief_state = contracts::BeliefState{
      .request_id = request.request_id,
      .confirmed_facts = std::vector<std::string>{"tool projection produced an observation"},
      .hypotheses = std::vector<std::string>{"response builder can finalize the turn"},
      .assumptions = std::vector<std::string>{"observation payload is user-safe"},
      .evidence_refs = std::vector<std::string>{latest_observation.observation_id.value_or(std::string{"obs-live-unary"})},
      .confidence = 0.85F,
      .goal_id = goal_id,
      .created_at = current_time_ms(),
      .tags = std::vector<std::string>{"runtime", "reflection", "true-integration"},
  };
  reflection_request.latest_observation = latest_observation;
  return reflection_request;
}

[[nodiscard]] cognition::ResponseBuildRequest make_response_build_request(
    const contracts::AgentRequest& request,
    const std::string& goal_id,
    const OrchestratorComposition& composition,
    const contracts::ContextPacket& context_packet,
    const std::optional<contracts::Observation>& latest_observation,
    const cognition::decision::ActionDecision& action_decision) {
  cognition::ResponseBuildRequest response_request;
  std::vector<std::string> confirmed_facts;
  std::vector<std::string> hypotheses;
  std::vector<std::string> assumptions;
  std::vector<std::string> evidence_refs;

  if (latest_observation.has_value()) {
    confirmed_facts.push_back("tool projection produced an observation");
    hypotheses.push_back("response builder can finalize the turn");
    assumptions.push_back("observation payload is user-safe");
    if (latest_observation->observation_id.has_value() &&
        !latest_observation->observation_id->empty()) {
      evidence_refs.push_back(*latest_observation->observation_id);
    }
  } else {
    confirmed_facts.push_back(
        "cognition selected a terminal response decision without a tool round");
    hypotheses.push_back("response builder can finalize the cognition terminal decision");
    assumptions.push_back("terminal decision outline is user-safe");
    evidence_refs.push_back(
        request.request_id.value_or(std::string{"req-live-unary"}) +
        ":cognition-terminal-decision");
  }

  response_request.caller_domain = "runtime.agent_orchestrator";
  response_request.request_id = request.request_id.value_or(std::string{"req-live-unary"});
  response_request.trace_id = request.trace_id.value_or(std::string{"trace-live-unary"});
  response_request.profile_id = composition.profile_id;
  response_request.goal_contract = make_goal_contract(request, goal_id);
  response_request.context_packet = context_packet;
  response_request.belief_state = contracts::BeliefState{
      .request_id = request.request_id,
      .confirmed_facts = std::move(confirmed_facts),
      .hypotheses = std::move(hypotheses),
      .assumptions = std::move(assumptions),
      .evidence_refs = std::move(evidence_refs),
      .confidence = 0.85F,
      .goal_id = goal_id,
      .created_at = current_time_ms(),
      .tags = std::vector<std::string>{"runtime", "response", "true-integration"},
  };
  response_request.latest_observation = latest_observation;
  response_request.terminal_decision = action_decision;
  response_request.build_hints.prefer_observation_projection = true;
  return response_request;
}

void finalize_live_agent_result(contracts::AgentResult* agent_result,
                                const contracts::AgentRequest& request,
                                const std::string& goal_id,
                                const std::optional<std::string>& checkpoint_ref) {
  if (agent_result == nullptr) {
    return;
  }

  if (!agent_result->request_id.has_value()) {
    agent_result->request_id = request.request_id;
  }
  if (!agent_result->trace_id.has_value()) {
    agent_result->trace_id = request.trace_id;
  }
  agent_result->goal_id = goal_id;
  agent_result->checkpoint_ref = checkpoint_ref;
  if (!agent_result->created_at.has_value()) {
    agent_result->created_at = current_time_ms();
  }
}

[[nodiscard]] std::string effective_goal_id(
    const contracts::AgentRequest& request,
    const OrchestratorComposition& composition) {
  if (request.goal_hint.has_value() && !request.goal_hint->empty()) {
    return *request.goal_hint;
  }

  return composition.default_goal_id;
}

[[nodiscard]] contracts::ErrorInfo make_runtime_error(
    const std::int32_t code,
    std::string message,
    const std::string& stage,
    const RuntimeState final_state) {
  return contracts::ErrorInfo{
      .failure_type = contracts::ResultCodeCategory::Runtime,
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = code,
          .message = std::move(message),
          .stage = stage,
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "runtime.agent_orchestrator",
          .ref_id = std::string{runtime_state_name(final_state)},
      },
  };
}

[[nodiscard]] contracts::AgentResult make_result(
    const contracts::AgentRequest& request,
    const RuntimeState final_state,
    const contracts::AgentResultStatus status,
    const std::int32_t result_code,
    const std::string& response_text,
    const std::optional<contracts::ErrorInfo>& error_info = std::nullopt,
    const std::optional<std::string>& checkpoint_ref = std::nullopt,
    const std::optional<std::string>& goal_id = std::nullopt) {
  contracts::AgentResult result;
  result.result_id = make_result_id(final_state);
  result.status = status;
  result.result_code = result_code;
  result.response_text = response_text;
  result.task_completed = (status == contracts::AgentResultStatus::Completed);
  result.created_at = current_time_ms();
  result.request_id = request.request_id;
  result.trace_id = request.trace_id;
  result.error_info = error_info;
  result.checkpoint_ref = checkpoint_ref;
  result.goal_id = goal_id;
  return result;
}

struct TransitionStep {
  RuntimeState to_state = RuntimeState::Idle;
  std::string reason;
  std::vector<TransitionGuardFact> guards;
};

struct TransitionFailure {
  RuntimeState state_before = RuntimeState::Idle;
  std::string stage;
  std::string detail;
};

[[nodiscard]] std::optional<TransitionFailure> apply_step(
    IAgentFsm& fsm,
    const std::string& stage,
    const TransitionStep& step,
    StateTransitionOutcome* accepted_outcome = nullptr) {
  const RuntimeState before = fsm.current_state();
  const StateTransitionRequest request{
      .from_state = before,
      .requested_to = step.to_state,
      .transition_reason = step.reason,
      .guard_facts = step.guards,
  };

  const auto outcome = fsm.transition(request);
  if (outcome.accepted) {
    if (accepted_outcome != nullptr) {
      *accepted_outcome = outcome;
    }
    return std::nullopt;
  }

  std::string detail = "transition rejected";
  if (outcome.rejection_reason.has_value()) {
    detail = outcome.rejection_reason->detail;
  }

  return TransitionFailure{
      .state_before = before,
      .stage = stage,
      .detail = std::move(detail),
  };
}

[[nodiscard]] std::optional<TransitionFailure> apply_steps(
    IAgentFsm& fsm,
    const std::string& stage,
    const std::vector<TransitionStep>& steps,
    StateTransitionOutcome* last_outcome = nullptr) {
  for (const auto& step : steps) {
    StateTransitionOutcome accepted_outcome;
    if (const auto failure = apply_step(fsm, stage, step, &accepted_outcome); failure.has_value()) {
      return failure;
    }

    if (last_outcome != nullptr) {
      *last_outcome = accepted_outcome;
    }
  }

  return std::nullopt;
}

void push_trace(std::vector<OrchestratorStageTrace>* trace,
                const OrchestratorStage stage,
                const RuntimeState before,
                const RuntimeState after,
                const bool entered,
                std::string detail) {
  trace->push_back(OrchestratorStageTrace{
      .stage = stage,
      .state_before = before,
      .state_after = after,
      .entered = entered,
      .detail = std::move(detail),
  });
}

[[nodiscard]] PendingInteractionState make_pending_interaction(
    const OrchestratorComposition& composition,
    const PendingInteractionKind interaction_kind,
    const std::string& blocking_reason) {
  return PendingInteractionState{
      .interaction_kind = interaction_kind,
      .prompt_token = composition.waiting_prompt_token + "-" +
                      pending_interaction_kind_name(interaction_kind),
      .deadline_ms = static_cast<std::int64_t>(
          current_time_ms() + pending_interaction_timeout_ms(composition)),
      .blocking_reason = blocking_reason,
      .resume_channel = composition.waiting_resume_channel,
      .input_schema_hint = composition.waiting_input_schema_hint,
  };
}

struct SavedCheckpointResult {
  std::optional<contracts::Checkpoint> checkpoint;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool saved() const {
    return checkpoint.has_value() && !error_code.has_value();
  }
};

[[nodiscard]] SavedCheckpointResult build_and_save_checkpoint(
    CheckpointManager& checkpoint_manager,
    const CheckpointBuildRequest& request) {
  const auto build_result = checkpoint_manager.build_checkpoint(request);
  if (!build_result.built()) {
    return SavedCheckpointResult{
        .checkpoint = std::nullopt,
        .error_code = build_result.report.error_code,
        .detail = build_result.report.detail,
    };
  }

  const auto persist_result = checkpoint_manager.save(
      *build_result.checkpoint,
      build_result.runtime_budget_snapshot);
  if (persist_result.failed()) {
    return SavedCheckpointResult{
        .checkpoint = std::nullopt,
        .error_code = persist_result.error_code,
        .detail = persist_result.detail,
    };
  }

  return SavedCheckpointResult{
      .checkpoint = build_result.checkpoint,
      .error_code = std::nullopt,
      .detail = persist_result.detail,
  };
}

struct SessionUpdateResult {
  std::optional<SessionSnapshot> snapshot;
  std::optional<RuntimeErrorCode> error_code;
  std::string detail;

  [[nodiscard]] bool updated() const {
    return snapshot.has_value() && !error_code.has_value();
  }
};

[[nodiscard]] SessionUpdateResult bind_session_checkpoint(
    SessionManager& session_manager,
    SessionSnapshot session_snapshot,
    const std::string& checkpoint_ref,
    const RuntimeState fsm_state,
    const std::optional<PendingInteractionState>& pending_interaction) {
  const auto persist_result = session_manager.bind_checkpoint_ref(
      BindCheckpointRefRequest{
          .session_id = session_snapshot.session_id,
          .request_id = session_snapshot.request_id,
          .checkpoint_ref = checkpoint_ref,
          .fsm_state = fsm_state,
          .pending_interaction = pending_interaction,
      });
  if (!persist_result.persisted) {
    return SessionUpdateResult{
        .snapshot = std::nullopt,
        .error_code = persist_result.error_code,
        .detail = persist_result.detail,
    };
  }

  session_snapshot.active_checkpoint_ref = checkpoint_ref;
  session_snapshot.fsm_state = fsm_state;
  session_snapshot.pending_interaction = pending_interaction;
  return SessionUpdateResult{
      .snapshot = session_snapshot,
      .error_code = std::nullopt,
      .detail = persist_result.detail,
  };
}

[[nodiscard]] SessionUpdateResult persist_terminal_session(
    SessionManager& session_manager,
    SessionSnapshot session_snapshot,
    const RuntimeState terminal_state,
    const std::optional<std::string>& checkpoint_ref,
    const std::string& audit_summary) {
  session_snapshot.pending_interaction = std::nullopt;
  session_snapshot.last_result_summary = audit_summary;
  const auto persist_result = session_manager.persist_turn(
      SessionPersistRequest{
          .session_snapshot = session_snapshot,
          .terminal_state = terminal_state,
          .checkpoint_ref = checkpoint_ref,
          .audit_summary = audit_summary,
          .next_resume_seed_ref = std::nullopt,
      });
  if (!persist_result.persisted) {
    return SessionUpdateResult{
        .snapshot = std::nullopt,
        .error_code = persist_result.error_code,
        .detail = persist_result.detail,
    };
  }

  session_snapshot.fsm_state = terminal_state;
  if (checkpoint_ref.has_value()) {
    session_snapshot.active_checkpoint_ref = checkpoint_ref;
  }
  return SessionUpdateResult{
      .snapshot = session_snapshot,
      .error_code = std::nullopt,
      .detail = persist_result.detail,
  };
}

[[nodiscard]] contracts::RecoveryRequest make_abort_safe_recovery_request(
    const contracts::AgentRequest& request,
    const contracts::Checkpoint& checkpoint,
    const contracts::BudgetSnapshot& budget_snapshot,
    const std::string& goal_id) {
  const auto tool_call_id = make_tool_call_id(request);
  const auto error_info = contracts::ErrorInfo{
      .failure_type = contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = 5003,
          .message = "tool round requires fail-safe recovery",
          .stage = "tool_round",
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "tool_call",
          .ref_id = tool_call_id,
      },
  };

  return contracts::RecoveryRequest{
      .reflection_decision = contracts::ReflectionDecision{
          .request_id = request.request_id,
          .decision_kind = contracts::ReflectionDecisionKind::AbortSafe,
          .rationale = std::string("runtime-local tool round escalated to abort_safe"),
          .goal_id = goal_id,
          .confidence = 1.0F,
          .relevant_observation_refs = std::vector<std::string>{"obs-tool-abort-safe"},
          .hint_ref = std::string("hint-abort-safe"),
          .created_at = current_time_ms(),
          .tags = std::vector<std::string>{"unit=orchestrator"},
      },
      .error_info = error_info,
      .latest_observation = contracts::Observation{
          .observation_id = std::string("obs-tool-abort-safe"),
          .source = contracts::ObservationSource::ToolExecution,
          .success = false,
          .payload = std::string("{}"),
          .created_at = current_time_ms(),
          .error = error_info,
          .side_effects = std::nullopt,
          .tool_call_id = tool_call_id,
          .worker_task_id = std::nullopt,
          .request_id = request.request_id,
          .goal_id = goal_id,
          .duration_ms = 12,
          .tags = std::vector<std::string>{"unit=orchestrator"},
      },
      .checkpoint = checkpoint,
      .idempotency_and_side_effect_report = contracts::IdempotencyAndSideEffectReport{
          .replay_safe = true,
          .idempotency_key = make_recovery_binding_token(request, checkpoint),
          .side_effects_present = false,
          .non_replayable_reason = std::nullopt,
      },
      .retry_count = 1,
      .runtime_budget_snapshot = budget_snapshot,
  };
}

    [[nodiscard]] contracts::BudgetSnapshot make_exhausted_budget_snapshot(
      contracts::BudgetSnapshot budget_snapshot,
      const contracts::BudgetType budget_type,
      std::string detail) {
      for (auto& entry : budget_snapshot.entries) {
      if (entry.budget_type == budget_type) {
        entry.current = entry.max + 1;
        entry.remaining = -1;
        entry.reject_reason = detail;
        budget_snapshot.overall_reject_reason = entry.reject_reason;
        return budget_snapshot;
      }
      }

      budget_snapshot.overall_reject_reason = std::move(detail);
      return budget_snapshot;
    }

    [[nodiscard]] contracts::RecoveryRequest make_budget_exhausted_recovery_request(
      const contracts::AgentRequest& request,
      const contracts::Checkpoint& checkpoint,
      contracts::BudgetSnapshot budget_snapshot,
      const std::string& goal_id) {
      const auto tool_call_id = make_tool_call_id(request);
      auto exhausted_budget_snapshot = make_exhausted_budget_snapshot(
        std::move(budget_snapshot),
        contracts::BudgetType::ToolCall,
        "runtime-local tool-call budget exhausted");

      const auto error_info = contracts::ErrorInfo{
        .failure_type = contracts::ResultCodeCategory::Runtime,
        .retryable = true,
        .safe_to_replan = false,
        .details = contracts::ErrorDetails{
            .code = 5001,
            .message = "tool round exhausted runtime budget",
          .stage = "tool_round",
        },
        .source_ref = contracts::ErrorSourceRefMinimal{
            .ref_type = "tool_call",
            .ref_id = tool_call_id,
        },
      };

      return contracts::RecoveryRequest{
        .reflection_decision = contracts::ReflectionDecision{
          .request_id = request.request_id,
          .decision_kind = contracts::ReflectionDecisionKind::AbortSafe,
          .rationale = std::string("runtime-local budget exhaustion escalated to degrade"),
          .goal_id = goal_id,
          .confidence = 1.0F,
          .relevant_observation_refs = std::vector<std::string>{"obs-tool-budget-exhausted"},
          .hint_ref = std::string("hint-budget-exhausted"),
          .created_at = current_time_ms(),
          .tags = std::vector<std::string>{"unit=orchestrator", "recovery=budget_exhausted"},
        },
        .error_info = error_info,
        .latest_observation = contracts::Observation{
          .observation_id = std::string("obs-tool-budget-exhausted"),
          .source = contracts::ObservationSource::ToolExecution,
          .success = false,
          .payload = std::string("{}"),
          .created_at = current_time_ms(),
          .error = error_info,
          .side_effects = std::nullopt,
          .tool_call_id = tool_call_id,
          .worker_task_id = std::nullopt,
          .request_id = request.request_id,
          .goal_id = goal_id,
          .duration_ms = 12,
          .tags = std::vector<std::string>{"unit=orchestrator", "recovery=budget_exhausted"},
        },
        .checkpoint = checkpoint,
        .idempotency_and_side_effect_report = contracts::IdempotencyAndSideEffectReport{
          .replay_safe = true,
          .idempotency_key = make_recovery_binding_token(request, checkpoint),
          .side_effects_present = false,
          .non_replayable_reason = std::nullopt,
        },
        .retry_count = 1,
        .runtime_budget_snapshot = std::move(exhausted_budget_snapshot),
      };
    }

[[nodiscard]] BudgetViolationClass exhausted_budget_violation(
    const contracts::BudgetType budget_type) {
  switch (budget_type) {
    case contracts::BudgetType::Token:
      return BudgetViolationClass::TokenExhausted;
    case contracts::BudgetType::Turn:
      return BudgetViolationClass::TurnExhausted;
    case contracts::BudgetType::ToolCall:
      return BudgetViolationClass::ToolCallExhausted;
    case contracts::BudgetType::Latency:
      return BudgetViolationClass::LatencyExhausted;
    case contracts::BudgetType::Replan:
      return BudgetViolationClass::ReplanExhausted;
  }

  return BudgetViolationClass::SnapshotUnavailable;
}

[[nodiscard]] std::optional<BudgetDecision> exhausted_budget_decision(
    const std::optional<contracts::BudgetSnapshot>& budget_snapshot) {
  if (!budget_snapshot.has_value()) {
    return std::nullopt;
  }

  for (const auto& entry : budget_snapshot->entries) {
    if (entry.current > entry.max || entry.reject_reason.has_value()) {
      return make_budget_rejected_decision(
          exhausted_budget_violation(entry.budget_type),
          entry.reject_reason.value_or(
              budget_snapshot->overall_reject_reason.value_or(std::string("budget exhausted"))),
          entry.budget_type);
    }
  }

  if (budget_snapshot->overall_reject_reason.has_value()) {
    return make_budget_rejected_decision(
        BudgetViolationClass::SnapshotUnavailable,
        *budget_snapshot->overall_reject_reason);
  }

  return std::nullopt;
}

[[nodiscard]] SafeModeDecision evaluate_safe_mode_for_recovery(
    SafeModeController& safe_mode_controller,
    const contracts::RecoveryOutcome& recovery_outcome,
    const std::optional<contracts::BudgetSnapshot>& budget_snapshot) {
  const auto budget_decision = exhausted_budget_decision(budget_snapshot);
  return safe_mode_controller.evaluate_entry(
      SafeModeTrigger{
          .trigger_kind = budget_decision.has_value()
                              ? SafeModeTriggerKind::BudgetExhausted
                              : SafeModeTriggerKind::RecoveryExhausted,
          .budget_decision = budget_decision,
          .recovery_outcome = recovery_outcome,
          .error_code = std::nullopt,
          .health_signal = std::nullopt,
          .detail = budget_decision.has_value()
                        ? budget_decision->detail
                        : recovery_outcome.escalation_reason.value_or(
                              std::string("recovery exhausted without explicit escalation reason")),
      });
}

[[nodiscard]] std::int32_t safe_terminal_result_code(const RuntimeState terminal_state) {
  switch (terminal_state) {
    case RuntimeState::Degraded:
      return kRuntimeOrchestratorDegradedCode;
    case RuntimeState::SafeMode:
      return kRuntimeOrchestratorSafeModeCode;
    case RuntimeState::FailedSafe:
    default:
      return kRuntimeOrchestratorSkeletonFailedSafeCode;
  }
}

[[nodiscard]] std::string safe_terminal_label(const RuntimeState terminal_state) {
  switch (terminal_state) {
    case RuntimeState::Degraded:
      return "degraded";
    case RuntimeState::SafeMode:
      return "safe-mode";
    case RuntimeState::FailedSafe:
    default:
      return "fail-safe";
  }
}

}  // namespace

const char* orchestrator_stage_name(const OrchestratorStage stage) {
  switch (stage) {
    case OrchestratorStage::Preflight:
      return "preflight";
    case OrchestratorStage::MainLoop:
      return "main_loop";
    case OrchestratorStage::ToolRound:
      return "tool_round";
    case OrchestratorStage::RecoveryRound:
      return "recovery_round";
    case OrchestratorStage::Terminalize:
      return "terminalize";
  }

  return "unknown";
}

AgentOrchestrator::AgentOrchestrator(OrchestratorComposition composition)
    : composition_(std::move(composition)),
      scheduler_(2, 16),
      safe_mode_controller_(composition_.policy_snapshot) {
  if (composition_.dependency_set != nullptr &&
      composition_.dependency_set->durable_state_root.has_value() &&
      !composition_.dependency_set->durable_state_root->empty()) {
    checkpoint_manager_.set_durable_state_root(
        composition_.dependency_set->durable_state_root);
    session_manager_.set_durable_state_root(
        composition_.dependency_set->durable_state_root);
  }
}

void AgentOrchestrator::seed_for_test(
    const std::optional<SessionSnapshot>& session_snapshot,
    const std::vector<contracts::Checkpoint>& checkpoints) {
  for (const auto& checkpoint : checkpoints) {
    checkpoint_manager_.seed_for_test(checkpoint);
  }

  if (session_snapshot.has_value()) {
    session_manager_.seed_for_test(*session_snapshot);
  }
}

void AgentOrchestrator::seed_checkpoint_for_test(
    const contracts::Checkpoint& checkpoint,
    std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot) {
  checkpoint_manager_.seed_for_test(checkpoint, std::move(runtime_budget_snapshot));
}

std::unique_ptr<IAgentFsm> AgentOrchestrator::build_fsm(const RuntimeState initial_state) const {
  if (composition_.fsm_factory) {
    return composition_.fsm_factory(initial_state);
  }

  return std::make_unique<AgentFsm>(initial_state);
}

OrchestratorRunResult AgentOrchestrator::run_once(const contracts::AgentRequest& request) {
  OrchestratorRunResult run_result;
  const auto normalized_request = normalize_request(request, next_sequence_++);
  const auto goal_id = effective_goal_id(normalized_request, composition_);
  const auto runtime_budget = effective_runtime_budget(normalized_request, composition_);

  auto fsm = build_fsm(RuntimeState::Idle);
  if (!fsm) {
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator cannot build FSM",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           "fsm factory returned null",
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const RuntimeState initial_state = fsm->current_state();
  if (composition_.stub_ports.reject_preflight) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               initial_state,
               initial_state,
               true,
               "stub preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               initial_state,
               initial_state,
               false,
               "skipped because preflight rejected request");
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonPreflightRejectedCode,
        "runtime orchestrator rejected request during preflight",
        make_runtime_error(kRuntimeOrchestratorSkeletonPreflightRejectedCode,
                           "stub preflight rejected request",
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto load_result = session_manager_.load_session(
      SessionLoadRequest{
          .session_id = *normalized_request.session_id,
          .request_id = *normalized_request.request_id,
          .checkpoint_ref = std::nullopt,
          .allow_session_create = true,
      });
  if (!load_result.has_snapshot()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               initial_state,
               initial_state,
               true,
               load_result.detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator failed to load session",
        make_runtime_error(static_cast<std::int32_t>(kRuntimeOrchestratorSkeletonInternalErrorCode),
                           load_result.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto prepare_turn_result = session_manager_.prepare_turn(
      PrepareTurnRequest{
          .session_snapshot = *load_result.snapshot,
          .resume_turn = false,
          .expected_checkpoint_ref = std::nullopt,
      });
  if (!prepare_turn_result.accepted || !prepare_turn_result.effective_session.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               initial_state,
               initial_state,
               true,
               prepare_turn_result.detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator failed to prepare session turn",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           prepare_turn_result.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  SessionSnapshot session_snapshot = *prepare_turn_result.effective_session;
  const auto budget_decision = budget_controller_.initialize(
      BudgetInitializeRequest{
        .runtime_budget = runtime_budget,
          .started_at_ms = composition_.budget_started_at_ms,
      });
  if (budget_decision.rejected()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               initial_state,
               initial_state,
               true,
               budget_decision.detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator failed to initialize budget",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           budget_decision.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const RuntimeState preflight_before = fsm->current_state();
  if (const auto failure = apply_steps(
          *fsm,
          orchestrator_stage_name(OrchestratorStage::Preflight),
          {{.to_state = RuntimeState::Receiving,
            .reason = "accept unary request",
            .guards = {TransitionGuardFact::AgentRequestAvailable,
                       TransitionGuardFact::FacadeInitialized}},
           {.to_state = RuntimeState::Planning,
            .reason = "preflight finished",
            .guards = {TransitionGuardFact::RequestValidated,
                       TransitionGuardFact::SessionLoaded,
                       TransitionGuardFact::BudgetInitialized}}});
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Preflight,
               preflight_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator hit an illegal preflight transition",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }
  push_trace(&run_result.stage_trace,
             OrchestratorStage::Preflight,
             preflight_before,
             fsm->current_state(),
             true,
             load_result.created_new_session
                 ? "session created, budget initialized, preflight admitted"
                 : "session loaded, budget initialized, preflight admitted");

  if (has_live_unary_ports(composition_) && has_production_llm_direct_path(composition_)) {
    auto context_result = composition_.dependency_set->memory_manager->prepare_context(
        make_memory_context_request(normalized_request, goal_id, composition_, runtime_budget));
    if (context_result.result_code.has_value() ||
      live_context_degradation_is_fatal(context_result)) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 fsm->current_state(),
                 fsm->current_state(),
                 true,
                 context_result.result_code.has_value()
                     ? "memory context assembly returned a failure result before llm dispatch"
                     : "memory context assembly returned a degraded packet before llm dispatch");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime llm path could not assemble a non-degraded context packet",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             context_assembly_failure_detail(
                                 context_result,
                                 "runtime llm path requires a complete context packet"),
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const RuntimeState main_loop_before = fsm->current_state();
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            TransitionStep{.to_state = RuntimeState::Reasoning,
                           .reason = "context assembled before production llm response",
                           .guards = {TransitionGuardFact::ContextAssembled}});
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator could not enter llm reasoning",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto llm_result = composition_.dependency_set->llm_manager->generate(
      make_runtime_response_llm_request(
        normalized_request, composition_, context_result.context_packet, runtime_budget));
    if (!llm_result.response.has_value() || llm_result.resolved_route.empty()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 "production llm request failed; agent.dataset fallback is disabled");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime llm request failed; agent.dataset fallback is disabled",
          llm_result.error.has_value()
              ? llm_result.error
              : std::optional<contracts::ErrorInfo>(make_runtime_error(
                    kRuntimeOrchestratorLiveUnaryFailedCode,
                    "llm manager returned no response",
                    orchestrator_stage_name(OrchestratorStage::MainLoop),
                    RuntimeState::Failed)),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto response_text = make_llm_response_text(llm_result);

    StateTransitionOutcome responding_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            TransitionStep{.to_state = RuntimeState::Responding,
                           .reason = "production llm response materialized",
                           .guards = {TransitionGuardFact::DirectResponseReady}},
            &responding_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator could not enter llm responding",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto llm_writeback_result = composition_.dependency_set->memory_manager->write_back(
        make_llm_direct_writeback_request(normalized_request, goal_id, response_text));
    if (llm_writeback_result.result_code.has_value() || llm_writeback_result.degraded ||
        llm_writeback_result.partial) {
      const std::string writeback_detail = llm_writeback_result.result_code.has_value()
                                               ? "llm direct response memory writeback failed"
                                               : "llm direct response memory writeback degraded or partial";
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::Terminalize,
                 fsm->current_state(),
                 fsm->current_state(),
                 true,
                 writeback_detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime llm path could not persist the response through memory writeback",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             writeback_detail,
                             orchestrator_stage_name(OrchestratorStage::Terminalize),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               fsm->current_state(),
               fsm->current_state(),
               true,
               "llm direct response persisted through memory writeback");

    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               fsm->current_state(),
               true,
               "production run completed through ILLMManager response path");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because production llm response path does not use agent.dataset");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because production llm response path succeeded");

    const RuntimeState terminalize_before = fsm->current_state();
    StateTransitionOutcome completed_outcome;
    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::Terminalize),
            {{.to_state = RuntimeState::Auditing,
              .reason = "llm response materialized",
              .guards = {TransitionGuardFact::ResponseMaterialized}},
             {.to_state = RuntimeState::Persisting,
              .reason = "llm response audit committed",
              .guards = {TransitionGuardFact::AuditCommitted}},
             {.to_state = RuntimeState::Completed,
              .reason = "llm response persisted",
              .guards = {TransitionGuardFact::PersistenceConfirmed}}},
            &completed_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::Terminalize,
                 terminalize_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed during llm terminalize",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto final_checkpoint = build_and_save_checkpoint(
        checkpoint_manager_,
        CheckpointBuildRequest{
            .transition_outcome = completed_outcome,
            .checkpoint_id = std::string("chk-llm-complete-") + *normalized_request.request_id,
            .step_id = std::string("llm-complete-") + *normalized_request.request_id,
            .working_memory_snapshot = std::string("wm:llm:") + *normalized_request.request_id,
            .pending_action = std::string(),
            .request_id = normalized_request.request_id,
            .goal_id = goal_id,
            .belief_state_ref = composition_.default_belief_state_ref,
            .retry_count = 0,
            .created_at_ms = normalized_request.created_at,
            .runtime_budget_snapshot = budget_controller_.snapshot(),
            .tags = {"path=llm", "origin=ILLMManager"},
        });
    if (!final_checkpoint.saved()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::Terminalize,
                 terminalize_before,
                 fsm->current_state(),
                 true,
                 final_checkpoint.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to save llm completion checkpoint",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             final_checkpoint.detail,
                             orchestrator_stage_name(OrchestratorStage::Terminalize),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto persisted_session = persist_terminal_session(
        session_manager_,
        session_snapshot,
        RuntimeState::Completed,
        final_checkpoint.checkpoint->checkpoint_id,
        composition_.default_audit_summary + " llm response");
    if (persisted_session.updated()) {
      run_result.effective_session = persisted_session.snapshot;
    }
    run_result.checkpoint = final_checkpoint.checkpoint;
    run_result.final_state = fsm->current_state();
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               terminalize_before,
               fsm->current_state(),
               true,
               "llm response audited, checkpointed, and persisted");
    run_result.agent_result = make_result(
        normalized_request,
        run_result.final_state,
        contracts::AgentResultStatus::Completed,
        kRuntimeOrchestratorSkeletonCompletedCode,
        response_text,
        std::nullopt,
        final_checkpoint.checkpoint->checkpoint_id,
        goal_id);
    return run_result;
  }

  if (has_live_unary_ports(composition_)) {
    auto context_result = composition_.dependency_set->memory_manager->prepare_context(
        make_memory_context_request(normalized_request, goal_id, composition_, runtime_budget));
    if (context_result.result_code.has_value() ||
        live_context_degradation_is_fatal(context_result)) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 fsm->current_state(),
                 fsm->current_state(),
                 true,
                 context_result.result_code.has_value()
                     ? "memory context assembly returned a failure result"
                     : "memory context assembly returned a degraded packet");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path could not assemble a non-degraded context packet",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             "memory context assembly did not produce a ready packet",
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const RuntimeState main_loop_before = fsm->current_state();
    StateTransitionOutcome reasoning_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            TransitionStep{.to_state = RuntimeState::Reasoning,
                           .reason = "context prepared through memory manager",
                           .guards = {TransitionGuardFact::ContextAssembled}},
            &reasoning_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator hit an illegal main loop transition on the live unary path",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto continue_budget = budget_controller_.can_continue();
    if (continue_budget.rejected()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 continue_budget.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path exhausted its budget before cognition",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             continue_budget.detail,
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    auto cognition_result = composition_.dependency_set->cognition_engine->decide(
        make_cognition_step_request(normalized_request,
                                    goal_id,
                                    composition_,
                                    context_result.context_packet,
                                    runtime_budget));
    bool context_reload_attempted = false;
    std::string context_reload_detail;
    if ((!cognition_result.action_decision.has_value() ||
         cognition_result.action_decision->decision_kind !=
             cognition::decision::ActionDecisionKind::ExecuteAction) &&
        should_consume_context_signal(cognition_result) &&
        cognition_result.context_sufficiency.recommend_context_reload) {
      const auto replan_budget = budget_controller_.can_replan();
      if (!replan_budget.rejected()) {
        context_reload_attempted = true;
        (void)budget_controller_.consume(BudgetConsumeRequest{
            .budget_type = contracts::BudgetType::Replan,
            .amount = 1,
            .observed_at_ms = static_cast<std::uint64_t>(current_time_ms()),
            .detail = "context reload consumed one replan budget on the live unary path",
        });

        auto refreshed_context_result = composition_.dependency_set->memory_manager->prepare_context(
            make_context_reload_request(normalized_request,
                                        goal_id,
                                        composition_,
                                        runtime_budget,
                                        cognition_result.context_sufficiency));
        if (!refreshed_context_result.result_code.has_value() && !refreshed_context_result.degraded) {
          context_result = std::move(refreshed_context_result);
          cognition_result = composition_.dependency_set->cognition_engine->decide(
              make_cognition_step_request(normalized_request,
                                          goal_id,
                                          composition_,
                                          context_result.context_packet,
                                          runtime_budget));
          context_reload_detail =
              "cognition requested one bounded context reload before the final action decision";
        } else {
          context_reload_detail = refreshed_context_result.result_code.has_value()
                                      ? "cognition requested a context reload but memory refresh failed"
                                      : "cognition requested a context reload but refreshed context remained degraded";
        }
      } else {
        context_reload_detail =
            "cognition requested a context reload but runtime replan budget was exhausted";
      }
    }

    if (has_decision_error_action_conflict(cognition_result)) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 "cognition returned an executable action together with error_info/result_code");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path rejected a conflicting cognition decision",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             "cognition returned an executable action together with error_info/result_code",
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    std::string belief_writeback_detail;
    if (should_write_back_belief_hint(cognition_result.belief_update_hint)) {
      const auto writeback_result = composition_.dependency_set->memory_manager->write_back(
          make_belief_writeback_request(normalized_request,
                                       goal_id,
                                       *cognition_result.belief_update_hint,
                                       cognition_result));
      if (writeback_result.result_code.has_value()) {
        belief_writeback_detail = "cognition belief hint writeback failed best-effort";
      } else if (writeback_result.degraded || writeback_result.partial) {
        belief_writeback_detail = "cognition belief hint writeback completed as a partial success";
      } else {
        belief_writeback_detail = "cognition belief hint writeback committed through memory seam";
      }
    }

    const auto complete_live_response =
        [&](const cognition::ResponseBuildResult& response_build_result) -> bool {
      const RuntimeState terminalize_before = fsm->current_state();
      StateTransitionOutcome completed_outcome;
      if (const auto failure = apply_steps(
              *fsm,
              orchestrator_stage_name(OrchestratorStage::Terminalize),
              {{.to_state = RuntimeState::Auditing,
                .reason = "response materialized",
                .guards = {TransitionGuardFact::ResponseMaterialized}},
               {.to_state = RuntimeState::Persisting,
                .reason = "audit committed",
                .guards = {TransitionGuardFact::AuditCommitted}},
               {.to_state = RuntimeState::Completed,
                .reason = "persistence confirmed",
                .guards = {TransitionGuardFact::PersistenceConfirmed}}},
              &completed_outcome);
          failure.has_value()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::Terminalize,
                   terminalize_before,
                   failure->state_before,
                   true,
                   failure->detail);
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorSkeletonInternalErrorCode,
            "runtime live unary path failed during terminalize",
            make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                               failure->detail,
                               failure->stage,
                               RuntimeState::Failed),
            std::nullopt,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return false;
      }

      const auto final_checkpoint = build_and_save_checkpoint(
          checkpoint_manager_,
          CheckpointBuildRequest{
              .transition_outcome = completed_outcome,
              .checkpoint_id = std::string("chk-live-complete-") +
                               *normalized_request.request_id,
              .step_id = std::string("live-complete-") + *normalized_request.request_id,
              .working_memory_snapshot = std::string("wm:live-complete:") +
                                         *normalized_request.request_id,
              .pending_action = std::string(),
              .request_id = normalized_request.request_id,
              .goal_id = goal_id,
              .belief_state_ref = composition_.default_belief_state_ref,
              .retry_count = 0,
              .created_at_ms = normalized_request.created_at,
              .runtime_budget_snapshot = budget_controller_.snapshot(),
              .tags = {"path=live-success"},
          });
      if (!final_checkpoint.saved()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::Terminalize,
                   terminalize_before,
                   fsm->current_state(),
                   true,
                   final_checkpoint.detail);
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorSkeletonInternalErrorCode,
            "runtime live unary path failed to save the completion checkpoint",
            make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                               final_checkpoint.detail,
                               orchestrator_stage_name(OrchestratorStage::Terminalize),
                               RuntimeState::Failed),
            std::nullopt,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return false;
      }

      const auto persisted_session = persist_terminal_session(
          session_manager_,
          session_snapshot,
          RuntimeState::Completed,
          final_checkpoint.checkpoint->checkpoint_id,
          composition_.default_audit_summary + " live unary integration");
      if (persisted_session.updated()) {
        run_result.effective_session = persisted_session.snapshot;
      }
      run_result.checkpoint = final_checkpoint.checkpoint;
      run_result.final_state = fsm->current_state();
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::Terminalize,
                 terminalize_before,
                 fsm->current_state(),
                 true,
                 "live unary response audited, checkpointed, and persisted");
      run_result.agent_result = *response_build_result.agent_result;
      finalize_live_agent_result(&run_result.agent_result,
                                 normalized_request,
                                 goal_id,
                                 final_checkpoint.checkpoint->checkpoint_id);
      return true;
    };

    if (has_terminal_response_action(cognition_result.action_decision)) {
      const auto response_build_result = composition_.dependency_set->response_builder->build(
          make_response_build_request(normalized_request,
                                      goal_id,
                                      composition_,
                                      context_result.context_packet,
                                      std::nullopt,
                                      *cognition_result.action_decision));

      if (!response_build_result.agent_result.has_value()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::MainLoop,
                   main_loop_before,
                   fsm->current_state(),
                   true,
                   "response builder did not materialize an AgentResult for the terminal cognition decision");
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorLiveUnaryFailedCode,
            "runtime live unary path could not materialize a terminal cognition response",
            response_build_result.error_info.value_or(
                make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                                   "response builder returned no AgentResult for the terminal cognition decision",
                                   orchestrator_stage_name(OrchestratorStage::MainLoop),
                                   RuntimeState::Failed)),
            std::nullopt,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return run_result;
      }

      StateTransitionOutcome response_outcome;
      if (const auto failure = apply_step(
              *fsm,
              orchestrator_stage_name(OrchestratorStage::MainLoop),
              TransitionStep{.to_state = RuntimeState::Responding,
                             .reason = "cognition selected a terminal response decision",
                             .guards = {TransitionGuardFact::DirectResponseReady}},
              &response_outcome);
          failure.has_value()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::MainLoop,
                   main_loop_before,
                   failure->state_before,
                   true,
                   failure->detail);
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorSkeletonInternalErrorCode,
            "runtime live unary path could not enter responding for the terminal cognition decision",
            make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                               failure->detail,
                               failure->stage,
                               RuntimeState::Failed),
            std::nullopt,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return run_result;
      }

      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 [&context_reload_detail, &belief_writeback_detail, context_reload_attempted]() {
                   std::string detail =
                       context_reload_attempted && !context_reload_detail.empty()
                           ? context_reload_detail
                           : "cognition selected a terminal response decision on the live unary path";
                   if (!belief_writeback_detail.empty()) {
                     detail += "; ";
                     detail += belief_writeback_detail;
                   }
                   return detail;
                 }());
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 fsm->current_state(),
                 fsm->current_state(),
                 false,
                 "skipped because terminal cognition decision did not require scheduler or tool execution");
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::RecoveryRound,
                 fsm->current_state(),
                 fsm->current_state(),
                 false,
                 "skipped because terminal cognition decision did not require recovery");

      if (!complete_live_response(response_build_result)) {
        return run_result;
      }
      return run_result;
    }

    if (!cognition_result.action_decision.has_value() ||
        cognition_result.action_decision->decision_kind !=
        cognition::decision::ActionDecisionKind::ExecuteAction) {
      if (should_degrade_to_waiting_clarify(cognition_result)) {
        StateTransitionOutcome waiting_outcome;
        if (const auto failure = apply_step(
                *fsm,
                orchestrator_stage_name(OrchestratorStage::MainLoop),
                TransitionStep{.to_state = RuntimeState::WaitingClarify,
                               .reason = "clarification required after cognition context evaluation",
                               .guards = {TransitionGuardFact::ClarificationNeeded,
                                          TransitionGuardFact::ProfileAllowsClarify}},
                &waiting_outcome);
            failure.has_value()) {
          push_trace(&run_result.stage_trace,
                     OrchestratorStage::MainLoop,
                     main_loop_before,
                     failure->state_before,
                     true,
                     failure->detail);
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path could not enter waiting clarify after cognition signaled missing context",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 failure->detail,
                                 failure->stage,
                                 RuntimeState::Failed),
              std::nullopt,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        const auto clarify_text = clarification_response_text(composition_, cognition_result);
        const auto saved_waiting_checkpoint = build_and_save_checkpoint(
            checkpoint_manager_,
            CheckpointBuildRequest{
                .transition_outcome = waiting_outcome,
                .checkpoint_id = std::string("chk-live-clarify-") + *normalized_request.request_id,
                .step_id = std::string("live-clarify-") + *normalized_request.request_id,
                .working_memory_snapshot = std::string("wm:clarify:") + *normalized_request.request_id,
                .pending_action = clarify_text,
                .request_id = normalized_request.request_id,
                .goal_id = goal_id,
                .belief_state_ref = composition_.default_belief_state_ref,
                .retry_count = 0,
                .created_at_ms = normalized_request.created_at,
                .runtime_budget_snapshot = budget_controller_.snapshot(),
                .tags = {"path=live-clarify"},
            });
        if (!saved_waiting_checkpoint.saved()) {
          push_trace(&run_result.stage_trace,
                     OrchestratorStage::MainLoop,
                     main_loop_before,
                     fsm->current_state(),
                     true,
                     saved_waiting_checkpoint.detail);
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path failed to save a clarification checkpoint",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 saved_waiting_checkpoint.detail,
                                 orchestrator_stage_name(OrchestratorStage::MainLoop),
                                 RuntimeState::Failed),
              std::nullopt,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        const auto pending_interaction = make_pending_interaction(
            composition_, PendingInteractionKind::Clarify, clarify_text);
        const auto bound_session = bind_session_checkpoint(
            session_manager_,
            session_snapshot,
            saved_waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string()),
            RuntimeState::WaitingClarify,
            pending_interaction);
        if (!bound_session.updated()) {
          push_trace(&run_result.stage_trace,
                     OrchestratorStage::MainLoop,
                     main_loop_before,
                     fsm->current_state(),
                     true,
                     bound_session.detail);
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path failed to bind clarification checkpoint to session",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 bound_session.detail,
                                 orchestrator_stage_name(OrchestratorStage::MainLoop),
                                 RuntimeState::Failed),
              std::nullopt,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        const auto resume_seed_result = session_manager_.build_resume_seed(
            BuildResumeSeedRequest{
                .session_snapshot = *bound_session.snapshot,
                .checkpoint_ref =
                    saved_waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string()),
                .resume_token = make_resume_binding_token(
                    bound_session.snapshot->session_id,
                    saved_waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string())),
                .resume_reason = std::string("resume after runtime clarification"),
                .policy_snapshot_ref = composition_.default_policy_snapshot_ref,
            });
        const auto resume_plan_result =
            resume_seed_result.built()
                ? checkpoint_manager_.make_resume_plan(*saved_waiting_checkpoint.checkpoint,
                                                       *resume_seed_result.resume_seed)
                : checkpoint_manager_.make_resume_plan(*saved_waiting_checkpoint.checkpoint);

        run_result.effective_session = bound_session.snapshot;
        run_result.checkpoint = saved_waiting_checkpoint.checkpoint;
        if (resume_seed_result.built() && resume_plan_result.resumable &&
            resume_plan_result.plan.has_value()) {
          run_result.resume_plan = resume_plan_result.plan;
        }
        run_result.final_state = RuntimeState::WaitingClarify;

        std::string waiting_detail = context_reload_detail.empty()
                                         ? "cognition signaled missing context and runtime entered waiting clarify"
                                         : context_reload_detail;
        if (!belief_writeback_detail.empty()) {
          waiting_detail += "; ";
          waiting_detail += belief_writeback_detail;
        }
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::MainLoop,
                   main_loop_before,
                   fsm->current_state(),
                   true,
                   waiting_detail);
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::ToolRound,
                   fsm->current_state(),
                   fsm->current_state(),
                   false,
                   "skipped because clarification degrade keeps the live unary turn resumable");
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::RecoveryRound,
                   fsm->current_state(),
                   fsm->current_state(),
                   false,
                   "skipped because clarification degrade does not enter recovery");
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::Terminalize,
                   fsm->current_state(),
                   fsm->current_state(),
                   false,
                   "skipped because clarification degrade keeps the turn resumable");
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::WaitingClarify,
            contracts::AgentResultStatus::PartiallyCompleted,
            kRuntimeOrchestratorWaitingCode,
            clarify_text,
            std::nullopt,
            saved_waiting_checkpoint.checkpoint->checkpoint_id,
            goal_id);
        return run_result;
      }

      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 "cognition did not choose an executable action on the live unary path");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path requires cognition to select an executable action",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             "cognition returned a non-executable decision",
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               fsm->current_state(),
               true,
               [&context_reload_detail, &belief_writeback_detail, context_reload_attempted]() {
                 std::string detail = context_reload_attempted && !context_reload_detail.empty()
                                          ? context_reload_detail
                                          : "memory context assembly and cognition decide completed through live dependency ports";
                 if (!belief_writeback_detail.empty()) {
                   detail += "; ";
                   detail += belief_writeback_detail;
                 }
                 return detail;
               }());

    const RuntimeState tool_round_before = fsm->current_state();
    const auto tool_budget_decision = budget_controller_.can_call_tool();
    if (tool_budget_decision.rejected()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 tool_budget_decision.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path rejected the tool call due to budget",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             tool_budget_decision.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    StateTransitionOutcome to_tool_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::ToolCalling,
                           .reason = "cognition selected a tool action through live ports",
                           .guards = {TransitionGuardFact::ToolCallPlanned,
                                      TransitionGuardFact::BudgetAllowsToolCall}},
            &to_tool_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path could not enter tool calling",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto enqueue_result = scheduler_.enqueue(
        SchedulerTicketRequest{
            .ticket_id = std::string("ticket-") + *normalized_request.request_id,
            .request_id = *normalized_request.request_id,
            .session_id = normalized_request.session_id,
            .priority_class = SchedulerPriorityClass::ForegroundInteractive,
            .cancellation_token = CancellationToken{},
            .checkpoint_ref = std::nullopt,
            .queue_key = std::nullopt,
        });
    if (!enqueue_result.accepted || !enqueue_result.ticket.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 enqueue_result.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::FailedSafe,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonFailedSafeCode,
          composition_.stub_ports.fail_safe_response_text,
          make_runtime_error(kRuntimeOrchestratorSkeletonFailedSafeCode,
                             enqueue_result.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::FailedSafe),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::FailedSafe;
      return run_result;
    }

    const auto worker_result = scheduler_.acquire_worker(
        AcquireWorkerRequest{
            .worker_budget = WorkerLeaseBudget{.max_workers = 1, .busy_workers = 0},
            .preferred_priority_class = SchedulerPriorityClass::ForegroundInteractive,
            .preferred_ticket_id = enqueue_result.ticket->ticket_id,
        });
    if (!worker_result.acquired || !worker_result.ticket.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 worker_result.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::FailedSafe,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonFailedSafeCode,
          composition_.stub_ports.fail_safe_response_text,
          make_runtime_error(kRuntimeOrchestratorSkeletonFailedSafeCode,
                             worker_result.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::FailedSafe),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::FailedSafe;
      return run_result;
    }

    (void)budget_controller_.consume(BudgetConsumeRequest{
        .budget_type = contracts::BudgetType::ToolCall,
        .amount = 1,
        .observed_at_ms = composition_.budget_started_at_ms + 1,
        .detail = "tool call consumed through live integration path",
    });

    const auto tool_request = make_tool_request(normalized_request,
                          runtime_budget,
                          *cognition_result.action_decision,
                          goal_id,
                          composition_);
    const auto tool_envelope = composition_.dependency_set->tool_manager->invoke(
      tool_request,
        make_tool_invocation_context(normalized_request, composition_));
    if (!tool_envelope.has_projection() || !tool_envelope.tool_result.has_value() ||
        !tool_envelope.tool_result->success.value_or(false)) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 "tool manager did not produce a successful observation projection");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path could not complete a successful tool projection",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             tool_envelope.failure_reason_code.value_or(
                                 std::string{"tool projection unavailable"}),
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    StateTransitionOutcome waiting_external_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::WaitingExternal,
                           .reason = "tool invocation completed and is ready for observation folding",
                           .guards = {TransitionGuardFact::ToolDispatchSubmitted}},
            &waiting_external_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path could not enter waiting external",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto waiting_checkpoint = build_and_save_checkpoint(
        checkpoint_manager_,
        CheckpointBuildRequest{
            .transition_outcome = waiting_external_outcome,
            .checkpoint_id = std::string("chk-live-tool-") + *normalized_request.request_id,
            .step_id = std::string("live-tool-") + *normalized_request.request_id,
            .working_memory_snapshot = std::string("wm:live-tool:") + *normalized_request.request_id,
            .pending_action = std::string("wait for projected tool observation"),
            .request_id = normalized_request.request_id,
            .goal_id = goal_id,
            .belief_state_ref = composition_.default_belief_state_ref,
            .retry_count = 0,
            .created_at_ms = normalized_request.created_at,
            .runtime_budget_snapshot = budget_controller_.snapshot(),
            .tags = {"path=live-tool"},
        });
    if (!waiting_checkpoint.saved()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 waiting_checkpoint.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path failed to save the tool checkpoint",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             waiting_checkpoint.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto waiting_binding = bind_session_checkpoint(
        session_manager_,
        session_snapshot,
        waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string{}),
        RuntimeState::WaitingExternal,
        make_pending_interaction(
            composition_, PendingInteractionKind::WaitExternal, "await projected tool observation"));
    if (!waiting_binding.updated()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 waiting_binding.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path failed to bind the tool checkpoint",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             waiting_binding.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    session_snapshot = *waiting_binding.snapshot;

    StateTransitionOutcome reflecting_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::Reflecting,
                           .reason = "tool observation projection is available",
                           .guards = {TransitionGuardFact::ExternalResultAvailable}},
            &reflecting_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path could not enter reflection",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto release_result = scheduler_.release_worker(
        ReleaseWorkerRequest{
            .ticket = *worker_result.ticket,
            .worker_completed = true,
        });
    if (release_result.released) {
      run_result.scheduler_backpressure = release_result.backpressure_state;
    } else {
      run_result.scheduler_backpressure = scheduler_.backpressure_state();
    }

    const auto latest_observation = *tool_envelope.observation;
    const auto reflection_result = composition_.dependency_set->cognition_engine->reflect(
        make_reflection_request(normalized_request,
                                goal_id,
                                composition_,
                                context_result.context_packet,
                                latest_observation));

    if (reflection_result.error_info.has_value() || reflection_result.result_code.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 "scheduler, tool invocation, and reflection completed through live dependency ports");
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::RecoveryRound,
                 fsm->current_state(),
                 fsm->current_state(),
                 true,
                 "cognition reflection returned an error surface and runtime failed closed");
      run_result.used_tool_round = true;
      run_result.used_recovery_round = true;
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path rejected the cognition reflection result",
          reflection_result.error_info.value_or(
              make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                                 "cognition reflection returned result_code without a recoverable reflection_decision",
                                 orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                                 RuntimeState::Failed)),
          waiting_checkpoint.checkpoint->checkpoint_id,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    if (reflection_result.reflection_decision.has_value() &&
      reflection_result.reflection_decision->decision_kind !=
        contracts::ReflectionDecisionKind::Continue) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 "scheduler, tool invocation, and reflection completed through live dependency ports");

      const auto recovery_request = make_reflection_recovery_request(
          *waiting_checkpoint.checkpoint,
          budget_controller_.snapshot(),
          latest_observation,
          *reflection_result.reflection_decision,
          tool_request,
          tool_envelope);
      const auto recovery_plan = recovery_manager_.evaluate(recovery_request);
      run_result.recovery_outcome = recovery_manager_.execute(recovery_plan);
      const auto recovery_apply_result = recovery_manager_.apply(*run_result.recovery_outcome);

      const RuntimeState recovery_round_before = fsm->current_state();
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::RecoveryRound,
                 recovery_round_before,
                 recovery_round_before,
                 true,
                 recovery_plan.detail.empty()
                     ? "reflection decision was evaluated through RecoveryManager"
                     : recovery_plan.detail);
      run_result.used_tool_round = true;
      run_result.used_recovery_round = true;

      const auto executed_action =
          run_result.recovery_outcome->executed_action.value_or(std::string{});
      if (executed_action == "continue" || executed_action == "retry_step") {
        if (!recovery_plan.resume_plan.has_value()) {
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path could not resume after reflection recovery",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 "recovery manager admitted a resume action without a resume plan",
                                 orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                                 RuntimeState::Failed),
              waiting_checkpoint.checkpoint->checkpoint_id,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        auto resumed_result = continue_from_checkpoint(*recovery_plan.resume_plan, session_snapshot);
        resumed_result.used_tool_round = true;
        resumed_result.used_recovery_round = true;
        resumed_result.recovery_outcome = run_result.recovery_outcome;
        auto merged_trace = run_result.stage_trace;
        merged_trace.insert(merged_trace.end(),
                            resumed_result.stage_trace.begin(),
                            resumed_result.stage_trace.end());
        resumed_result.stage_trace = std::move(merged_trace);
        return resumed_result;
      }

      if (executed_action == "replan") {
        auto resumed_result = continue_from_checkpoint(
            make_reflection_replan_resume_plan(
                *waiting_checkpoint.checkpoint,
                reflection_result.reflection_decision->rationale.value_or(
                    std::string{"reflection requested a planning restart"})),
            session_snapshot);
        resumed_result.used_tool_round = true;
        resumed_result.used_recovery_round = true;
        resumed_result.recovery_outcome = run_result.recovery_outcome;
        auto merged_trace = run_result.stage_trace;
        merged_trace.insert(merged_trace.end(),
                            resumed_result.stage_trace.begin(),
                            resumed_result.stage_trace.end());
        resumed_result.stage_trace = std::move(merged_trace);
        return resumed_result;
      }

      if (executed_action == "abort_safe" || executed_action == "degrade") {
        const auto safe_mode_decision = evaluate_safe_mode_for_recovery(
            safe_mode_controller_,
            *run_result.recovery_outcome,
            recovery_request.runtime_budget_snapshot);
        const auto target_runtime_state =
            safe_mode_decision.target_runtime_state.value_or(RuntimeState::FailedSafe);

        StateTransitionOutcome terminal_outcome;
        std::optional<TransitionFailure> transition_failure;
        if (target_runtime_state == RuntimeState::SafeMode) {
          transition_failure = apply_steps(
              *fsm,
              orchestrator_stage_name(OrchestratorStage::RecoveryRound),
              {{.to_state = RuntimeState::Degraded,
                .reason = safe_mode_decision.detail,
                .guards = {TransitionGuardFact::RecoveryDegrade}},
               {.to_state = RuntimeState::SafeMode,
                .reason = safe_mode_decision.detail,
                .guards = {TransitionGuardFact::SafeModeTriggerSatisfied}}},
              &terminal_outcome);
        } else if (target_runtime_state == RuntimeState::Degraded) {
          transition_failure = apply_step(
              *fsm,
              orchestrator_stage_name(OrchestratorStage::RecoveryRound),
              TransitionStep{.to_state = RuntimeState::Degraded,
                             .reason = safe_mode_decision.detail,
                             .guards = {TransitionGuardFact::RecoveryDegrade}},
              &terminal_outcome);
        } else {
          transition_failure = apply_step(
              *fsm,
              orchestrator_stage_name(OrchestratorStage::RecoveryRound),
              TransitionStep{.to_state = RuntimeState::FailedSafe,
                             .reason = safe_mode_decision.detail,
                             .guards = {TransitionGuardFact::RecoveryAbortSafe}},
              &terminal_outcome);
        }

        if (transition_failure.has_value()) {
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path could not enter the safe terminal state after reflection recovery",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 transition_failure->detail,
                                 transition_failure->stage,
                                 RuntimeState::Failed),
              waiting_checkpoint.checkpoint->checkpoint_id,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        const auto checkpoint_label = safe_terminal_label(target_runtime_state);
        const auto terminal_checkpoint = build_and_save_checkpoint(
            checkpoint_manager_,
            CheckpointBuildRequest{
                .transition_outcome = terminal_outcome,
                .checkpoint_id = std::string("chk-") + checkpoint_label + "-" +
                                 *normalized_request.request_id,
                .step_id = checkpoint_label + "-" + *normalized_request.request_id,
                .working_memory_snapshot = std::string("wm:") + checkpoint_label + ":" +
                                           *normalized_request.request_id,
                .pending_action = std::string(),
                .request_id = normalized_request.request_id,
                .goal_id = goal_id,
                .belief_state_ref = composition_.default_belief_state_ref,
                .retry_count = 1,
                .created_at_ms = normalized_request.created_at,
                .runtime_budget_snapshot = budget_controller_.snapshot(),
                .tags = {std::string("path=") + checkpoint_label},
            });
        if (!terminal_checkpoint.saved()) {
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path failed to save reflection recovery terminal checkpoint",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 terminal_checkpoint.detail,
                                 orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                                 RuntimeState::Failed),
              waiting_checkpoint.checkpoint->checkpoint_id,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        run_result.checkpoint = terminal_checkpoint.checkpoint;

        if (const auto failure = apply_steps(
                *fsm,
                orchestrator_stage_name(OrchestratorStage::Terminalize),
                {{.to_state = RuntimeState::Responding,
                  .reason = std::string("emit ") + checkpoint_label + " response",
                  .guards = {}},
                 {.to_state = RuntimeState::Auditing,
                  .reason = "response materialized",
                  .guards = {TransitionGuardFact::ResponseMaterialized}},
                 {.to_state = RuntimeState::Persisting,
                  .reason = "audit committed",
                  .guards = {TransitionGuardFact::AuditCommitted}},
                 {.to_state = RuntimeState::Completed,
                  .reason = "persistence confirmed",
                  .guards = {TransitionGuardFact::PersistenceConfirmed}}});
            failure.has_value()) {
          run_result.agent_result = make_result(
              normalized_request,
              RuntimeState::Failed,
              contracts::AgentResultStatus::Failed,
              kRuntimeOrchestratorSkeletonInternalErrorCode,
              "runtime live unary path failed to terminalize the reflection recovery safe path",
              make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                                 failure->detail,
                                 failure->stage,
                                 RuntimeState::Failed),
              run_result.checkpoint->checkpoint_id,
              goal_id);
          run_result.final_state = RuntimeState::Failed;
          return run_result;
        }

        const auto persisted_session = persist_terminal_session(
            session_manager_,
            session_snapshot,
            target_runtime_state,
            run_result.checkpoint->checkpoint_id,
            composition_.default_audit_summary + " " + checkpoint_label);
        if (persisted_session.updated()) {
          run_result.effective_session = persisted_session.snapshot;
        }

        const auto safe_terminal_error_code = safe_mode_decision.error_code.has_value()
                                                  ? static_cast<std::int32_t>(
                                                        *safe_mode_decision.error_code)
                                                  : recovery_apply_result.error_code.has_value()
                                                        ? static_cast<std::int32_t>(
                                                              *recovery_apply_result.error_code)
                                                        : kRuntimeOrchestratorSkeletonFailedSafeCode;
        run_result.final_state = target_runtime_state;
        run_result.agent_result = make_result(
            normalized_request,
            run_result.final_state,
            contracts::AgentResultStatus::Failed,
            safe_terminal_result_code(target_runtime_state),
            composition_.stub_ports.fail_safe_response_text,
            make_runtime_error(safe_terminal_error_code,
                               safe_mode_decision.detail.empty() ? recovery_apply_result.detail
                                                                 : safe_mode_decision.detail,
                               orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                               target_runtime_state),
            run_result.checkpoint->checkpoint_id,
            goal_id);
        return run_result;
      }

      const auto recovery_detail = run_result.recovery_outcome->rejection_reason.value_or(
          run_result.recovery_outcome->escalation_reason.value_or(recovery_apply_result.detail));
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path rejected the reflection recovery outcome",
          make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                             recovery_detail,
                             orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                             RuntimeState::Failed),
          waiting_checkpoint.checkpoint->checkpoint_id,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    StateTransitionOutcome continue_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::Reasoning,
                           .reason = "reflection converged to final response",
                           .guards = {TransitionGuardFact::ReflectionContinue}},
            &continue_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path could not return to reasoning after reflection",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto response_build_result = composition_.dependency_set->response_builder->build(
        make_response_build_request(normalized_request,
                                    goal_id,
                                    composition_,
                                    context_result.context_packet,
                                    latest_observation,
                                    *cognition_result.action_decision));

    if (!response_build_result.agent_result.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 "response builder did not materialize an AgentResult");
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorLiveUnaryFailedCode,
          "runtime live unary path could not materialize a response",
          response_build_result.error_info.value_or(
              make_runtime_error(kRuntimeOrchestratorLiveUnaryFailedCode,
                                 "response builder returned no AgentResult",
                                 orchestrator_stage_name(OrchestratorStage::ToolRound),
                                 RuntimeState::Failed)),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    StateTransitionOutcome response_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::Responding,
                           .reason = "response builder materialized the terminal payload",
                           .guards = {TransitionGuardFact::DirectResponseReady}},
            &response_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime live unary path could not enter responding",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    run_result.used_tool_round = true;
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               tool_round_before,
               fsm->current_state(),
               true,
               "scheduler, tool invocation, reflection, and response builder completed through live dependency ports");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because the live unary success path did not require recovery");

    if (!complete_live_response(response_build_result)) {
      return run_result;
    }
    return run_result;
  }

  const RuntimeState main_loop_before = fsm->current_state();
  StateTransitionOutcome main_loop_outcome;
  if (const auto failure = apply_step(
          *fsm,
          orchestrator_stage_name(OrchestratorStage::MainLoop),
          TransitionStep{.to_state = RuntimeState::Reasoning,
                         .reason = "context prepared for reasoning",
                         .guards = {TransitionGuardFact::ContextAssembled}},
          &main_loop_outcome);
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator hit an illegal main loop transition",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto continue_budget = budget_controller_.can_continue();
  if (continue_budget.rejected()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               fsm->current_state(),
               true,
               continue_budget.detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator rejected main loop due to budget exhaustion",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           continue_budget.detail,
                           orchestrator_stage_name(OrchestratorStage::MainLoop),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  if (composition_.stub_ports.main_loop_exit == StubMainLoopExit::WaitingClarify) {
    StateTransitionOutcome waiting_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            TransitionStep{.to_state = RuntimeState::WaitingClarify,
                           .reason = "clarification required by runtime-local assembly",
                           .guards = {TransitionGuardFact::ClarificationNeeded,
                                      TransitionGuardFact::ProfileAllowsClarify}},
            &waiting_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator could not enter waiting clarify state",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto saved_waiting_checkpoint = build_and_save_checkpoint(
        checkpoint_manager_,
        CheckpointBuildRequest{
            .transition_outcome = waiting_outcome,
            .checkpoint_id = std::string("chk-wait-") + *normalized_request.request_id,
            .step_id = std::string("wait-clarify-") + *normalized_request.request_id,
            .working_memory_snapshot = std::string("wm:wait:") + *normalized_request.request_id,
            .pending_action = std::string("wait for user clarification"),
            .request_id = normalized_request.request_id,
            .goal_id = goal_id,
            .belief_state_ref = composition_.default_belief_state_ref,
            .retry_count = 0,
            .created_at_ms = normalized_request.created_at,
            .runtime_budget_snapshot = budget_controller_.snapshot(),
            .tags = {"path=waiting"},
        });
    if (!saved_waiting_checkpoint.saved()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 saved_waiting_checkpoint.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to save waiting checkpoint",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             saved_waiting_checkpoint.detail,
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto pending_interaction = make_pending_interaction(
        composition_, PendingInteractionKind::Clarify, "await clarification");
    const auto bound_session = bind_session_checkpoint(
        session_manager_,
        session_snapshot,
        saved_waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string()),
        RuntimeState::WaitingClarify,
        pending_interaction);
    if (!bound_session.updated()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 fsm->current_state(),
                 true,
                 bound_session.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to bind waiting checkpoint to session",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             bound_session.detail,
                             orchestrator_stage_name(OrchestratorStage::MainLoop),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto resume_seed_result = session_manager_.build_resume_seed(
        BuildResumeSeedRequest{
            .session_snapshot = *bound_session.snapshot,
            .checkpoint_ref = saved_waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string()),
        .resume_token = make_resume_binding_token(
          bound_session.snapshot->session_id,
          saved_waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string())),
            .resume_reason = std::string("resume after user clarification"),
            .policy_snapshot_ref = composition_.default_policy_snapshot_ref,
        });
    const auto resume_plan_result = resume_seed_result.built()
                      ? checkpoint_manager_.make_resume_plan(
                          *saved_waiting_checkpoint.checkpoint,
                          *resume_seed_result.resume_seed)
                      : checkpoint_manager_.make_resume_plan(
                          *saved_waiting_checkpoint.checkpoint);

    run_result.effective_session = bound_session.snapshot;
    run_result.checkpoint = saved_waiting_checkpoint.checkpoint;
    if (resume_seed_result.built() && resume_plan_result.resumable && resume_plan_result.plan.has_value()) {
      run_result.resume_plan = resume_plan_result.plan;
    }
    run_result.final_state = RuntimeState::WaitingClarify;
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               fsm->current_state(),
               true,
               "runtime-local assembly entered waiting clarify and produced resume plan");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because waiting clarify path does not use tools");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because waiting clarify path does not enter recovery");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because waiting clarify path keeps turn resumable");
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::WaitingClarify,
        contracts::AgentResultStatus::PartiallyCompleted,
        kRuntimeOrchestratorWaitingCode,
        composition_.stub_ports.waiting_response_text,
        std::nullopt,
        saved_waiting_checkpoint.checkpoint->checkpoint_id,
        goal_id);
    return run_result;
  }

  if (composition_.stub_ports.main_loop_exit == StubMainLoopExit::ToolRound) {
    StateTransitionOutcome to_tool_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            TransitionStep{.to_state = RuntimeState::ToolCalling,
                           .reason = "tool round selected by runtime-local assembly",
                           .guards = {TransitionGuardFact::ToolCallPlanned,
                                      TransitionGuardFact::BudgetAllowsToolCall}},
            &to_tool_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to enter tool round",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    run_result.used_tool_round = true;
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               fsm->current_state(),
               true,
               "runtime-local assembly routed to real scheduler and recovery controllers");

    const auto tool_budget_decision = budget_controller_.can_call_tool();
    if (tool_budget_decision.rejected()) {
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator rejected tool round due to budget",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             tool_budget_decision.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto enqueue_result = scheduler_.enqueue(
        SchedulerTicketRequest{
            .ticket_id = std::string("ticket-") + *normalized_request.request_id,
            .request_id = *normalized_request.request_id,
            .session_id = normalized_request.session_id,
            .priority_class = SchedulerPriorityClass::ForegroundInteractive,
            .cancellation_token = CancellationToken{},
            .checkpoint_ref = std::nullopt,
            .queue_key = std::nullopt,
        });
    if (!enqueue_result.accepted) {
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::FailedSafe,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonFailedSafeCode,
          composition_.stub_ports.fail_safe_response_text,
          make_runtime_error(kRuntimeOrchestratorSkeletonFailedSafeCode,
                             enqueue_result.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::FailedSafe),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::FailedSafe;
      return run_result;
    }

    const auto worker_result = scheduler_.acquire_worker(
        AcquireWorkerRequest{
            .worker_budget = WorkerLeaseBudget{.max_workers = 1, .busy_workers = 0},
            .preferred_priority_class = SchedulerPriorityClass::ForegroundInteractive,
            .preferred_ticket_id = enqueue_result.ticket->ticket_id,
        });
    if (!worker_result.acquired || !worker_result.ticket.has_value()) {
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::FailedSafe,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonFailedSafeCode,
          composition_.stub_ports.fail_safe_response_text,
          make_runtime_error(kRuntimeOrchestratorSkeletonFailedSafeCode,
                             worker_result.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::FailedSafe),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::FailedSafe;
      return run_result;
    }

    (void)budget_controller_.consume(BudgetConsumeRequest{
        .budget_type = contracts::BudgetType::ToolCall,
        .amount = 1,
        .observed_at_ms = composition_.budget_started_at_ms + 1,
        .detail = "tool call consumed",
    });

    const RuntimeState tool_round_before = fsm->current_state();
    StateTransitionOutcome waiting_external_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::WaitingExternal,
                           .reason = "tool dispatch submitted",
                           .guards = {TransitionGuardFact::ToolDispatchSubmitted}},
            &waiting_external_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator hit an illegal tool transition",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto waiting_checkpoint = build_and_save_checkpoint(
        checkpoint_manager_,
        CheckpointBuildRequest{
            .transition_outcome = waiting_external_outcome,
            .checkpoint_id = std::string("chk-tool-") + *normalized_request.request_id,
            .step_id = std::string("tool-") + *normalized_request.request_id,
            .working_memory_snapshot = std::string("wm:tool:") + *normalized_request.request_id,
            .pending_action = std::string("wait for tool result"),
            .request_id = normalized_request.request_id,
            .goal_id = goal_id,
            .belief_state_ref = composition_.default_belief_state_ref,
            .retry_count = 1,
            .created_at_ms = normalized_request.created_at,
            .runtime_budget_snapshot = budget_controller_.snapshot(),
            .tags = {"path=tool"},
        });
    if (!waiting_checkpoint.saved()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 waiting_checkpoint.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to save tool checkpoint",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             waiting_checkpoint.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto waiting_binding = bind_session_checkpoint(
        session_manager_,
        session_snapshot,
        waiting_checkpoint.checkpoint->checkpoint_id.value_or(std::string()),
        RuntimeState::WaitingExternal,
        make_pending_interaction(
            composition_, PendingInteractionKind::WaitExternal, "await external tool result"));
    if (!waiting_binding.updated()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 fsm->current_state(),
                 true,
                 waiting_binding.detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to bind tool checkpoint to session",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             waiting_binding.detail,
                             orchestrator_stage_name(OrchestratorStage::ToolRound),
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    session_snapshot = *waiting_binding.snapshot;

    StateTransitionOutcome reflecting_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::ToolRound),
            TransitionStep{.to_state = RuntimeState::Reflecting,
                           .reason = "external result available",
                           .guards = {TransitionGuardFact::ExternalResultAvailable}},
            &reflecting_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::ToolRound,
                 tool_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator could not enter reflecting state",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }

    const auto release_result = scheduler_.release_worker(
        ReleaseWorkerRequest{
            .ticket = *worker_result.ticket,
        .worker_completed = true,
        });
    if (release_result.released) {
      run_result.scheduler_backpressure = release_result.backpressure_state;
    } else {
      run_result.scheduler_backpressure = scheduler_.backpressure_state();
    }

    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               tool_round_before,
               fsm->current_state(),
               true,
               "runtime-local assembly routed request through scheduler and tool observation");

    const RuntimeState recovery_round_before = fsm->current_state();
    run_result.used_recovery_round = true;
    if (composition_.stub_ports.recovery_exit == StubRecoveryExit::AbortSafe ||
      composition_.stub_ports.recovery_exit == StubRecoveryExit::BudgetExhausted) {
      const auto recovery_request =
        composition_.stub_ports.recovery_exit == StubRecoveryExit::BudgetExhausted
          ? make_budget_exhausted_recovery_request(
            normalized_request,
            *waiting_checkpoint.checkpoint,
            budget_controller_.snapshot(),
            goal_id)
          : make_abort_safe_recovery_request(
            normalized_request,
            *waiting_checkpoint.checkpoint,
            budget_controller_.snapshot(),
            goal_id);
      const auto execution_plan = recovery_manager_.evaluate(recovery_request);
      run_result.recovery_outcome = recovery_manager_.execute(execution_plan);
      const auto apply_result = recovery_manager_.apply(*run_result.recovery_outcome);
      const auto safe_mode_decision = evaluate_safe_mode_for_recovery(
          safe_mode_controller_,
          *run_result.recovery_outcome,
          recovery_request.runtime_budget_snapshot);
      const auto target_runtime_state = safe_mode_decision.target_runtime_state.value_or(
          RuntimeState::FailedSafe);

      StateTransitionOutcome terminal_outcome;
      std::optional<TransitionFailure> transition_failure;
      if (target_runtime_state == RuntimeState::SafeMode) {
        transition_failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::RecoveryRound),
            {{.to_state = RuntimeState::Degraded,
              .reason = safe_mode_decision.detail,
              .guards = {TransitionGuardFact::RecoveryDegrade}},
             {.to_state = RuntimeState::SafeMode,
              .reason = safe_mode_decision.detail,
              .guards = {TransitionGuardFact::SafeModeTriggerSatisfied}}},
            &terminal_outcome);
      } else if (target_runtime_state == RuntimeState::Degraded) {
        transition_failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::RecoveryRound),
            TransitionStep{.to_state = RuntimeState::Degraded,
                           .reason = safe_mode_decision.detail,
                           .guards = {TransitionGuardFact::RecoveryDegrade}},
            &terminal_outcome);
      } else {
        transition_failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::RecoveryRound),
            TransitionStep{.to_state = RuntimeState::FailedSafe,
                           .reason = safe_mode_decision.detail,
                           .guards = {TransitionGuardFact::RecoveryAbortSafe}},
            &terminal_outcome);
      }

      if (transition_failure.has_value()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::RecoveryRound,
                   recovery_round_before,
                   transition_failure->state_before,
                   true,
                   transition_failure->detail);
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorSkeletonInternalErrorCode,
            "runtime orchestrator could not enter the safe terminal state after recovery escalation",
            make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                               transition_failure->detail,
                               transition_failure->stage,
                               RuntimeState::Failed),
            std::nullopt,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return run_result;
      }

      const auto checkpoint_label = safe_terminal_label(target_runtime_state);
      const auto terminal_checkpoint = build_and_save_checkpoint(
          checkpoint_manager_,
          CheckpointBuildRequest{
              .transition_outcome = terminal_outcome,
              .checkpoint_id = std::string("chk-") + checkpoint_label + "-" +
                               *normalized_request.request_id,
              .step_id = checkpoint_label + "-" + *normalized_request.request_id,
              .working_memory_snapshot = std::string("wm:") + checkpoint_label + ":" +
                                         *normalized_request.request_id,
              .pending_action = std::string(),
              .request_id = normalized_request.request_id,
              .goal_id = goal_id,
              .belief_state_ref = composition_.default_belief_state_ref,
              .retry_count = 1,
              .created_at_ms = normalized_request.created_at,
              .runtime_budget_snapshot = budget_controller_.snapshot(),
              .tags = {std::string("path=") + checkpoint_label},
          });
      if (!terminal_checkpoint.saved()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::RecoveryRound,
                   recovery_round_before,
                   fsm->current_state(),
                   true,
                   terminal_checkpoint.detail);
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorSkeletonInternalErrorCode,
            "runtime orchestrator failed to save safe terminal checkpoint",
            make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                               terminal_checkpoint.detail,
                               orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                               RuntimeState::Failed),
            std::nullopt,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return run_result;
      }
      run_result.checkpoint = terminal_checkpoint.checkpoint;
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::RecoveryRound,
                 recovery_round_before,
                 fsm->current_state(),
                 true,
                 safe_mode_decision.detail.empty() ? apply_result.detail : safe_mode_decision.detail);

      const RuntimeState terminalize_before = fsm->current_state();
      if (const auto failure = apply_steps(
              *fsm,
              orchestrator_stage_name(OrchestratorStage::Terminalize),
              {{.to_state = RuntimeState::Responding,
                .reason = std::string("emit ") + checkpoint_label + " response",
                .guards = {}},
               {.to_state = RuntimeState::Auditing,
                .reason = "response materialized",
                .guards = {TransitionGuardFact::ResponseMaterialized}},
               {.to_state = RuntimeState::Persisting,
                .reason = "audit committed",
                .guards = {TransitionGuardFact::AuditCommitted}},
               {.to_state = RuntimeState::Completed,
                .reason = "persistence confirmed",
                .guards = {TransitionGuardFact::PersistenceConfirmed}}});
          failure.has_value()) {
        push_trace(&run_result.stage_trace,
                   OrchestratorStage::Terminalize,
                   terminalize_before,
                   failure->state_before,
                   true,
                   failure->detail);
        run_result.agent_result = make_result(
            normalized_request,
            RuntimeState::Failed,
            contracts::AgentResultStatus::Failed,
            kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator failed to terminalize the safe terminal path",
            make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                               failure->detail,
                               failure->stage,
                               RuntimeState::Failed),
            run_result.checkpoint->checkpoint_id,
            goal_id);
        run_result.final_state = RuntimeState::Failed;
        return run_result;
      }

      const auto persisted_session = persist_terminal_session(
          session_manager_,
          session_snapshot,
          target_runtime_state,
          run_result.checkpoint->checkpoint_id,
          composition_.default_audit_summary + " " + checkpoint_label);
      if (persisted_session.updated()) {
        run_result.effective_session = persisted_session.snapshot;
      }
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::Terminalize,
                 terminalize_before,
                 fsm->current_state(),
                 true,
             std::string("safe terminal response audited and persisted as ") +
               runtime_state_name(target_runtime_state));
        run_result.final_state = target_runtime_state;
        const auto safe_terminal_error_code = safe_mode_decision.error_code.has_value()
                            ? static_cast<std::int32_t>(
                                *safe_mode_decision.error_code)
                            : apply_result.error_code.has_value()
                                ? static_cast<std::int32_t>(
                                  *apply_result.error_code)
                                : kRuntimeOrchestratorSkeletonFailedSafeCode;
      run_result.agent_result = make_result(
          normalized_request,
          run_result.final_state,
          contracts::AgentResultStatus::Failed,
          safe_terminal_result_code(target_runtime_state),
          composition_.stub_ports.fail_safe_response_text,
          make_runtime_error(safe_terminal_error_code,
                   safe_mode_decision.detail.empty()
                     ? apply_result.detail
                     : safe_mode_decision.detail,
                             orchestrator_stage_name(OrchestratorStage::RecoveryRound),
                   target_runtime_state),
          run_result.checkpoint->checkpoint_id,
          goal_id);
      return run_result;
    }

    StateTransitionOutcome continue_outcome;
    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::RecoveryRound),
            {{.to_state = RuntimeState::Reasoning,
              .reason = "reflection continue path",
              .guards = {TransitionGuardFact::ReflectionContinue}},
             {.to_state = RuntimeState::Responding,
              .reason = "response materialized after reflection",
              .guards = {TransitionGuardFact::DirectResponseReady}}},
            &continue_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::RecoveryRound,
                 recovery_round_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator could not continue after reflection",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               recovery_round_before,
               fsm->current_state(),
               true,
               "reflection continued to response path");
  } else {
    StateTransitionOutcome response_outcome;
    if (const auto failure = apply_step(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            TransitionStep{.to_state = RuntimeState::Responding,
                           .reason = "direct response path",
                           .guards = {TransitionGuardFact::DirectResponseReady}},
            &response_outcome);
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          normalized_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "runtime orchestrator could not materialize direct response",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               main_loop_before,
               fsm->current_state(),
               true,
               "runtime-local assembly produced direct response without tool round");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::ToolRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because direct response path did not require scheduler");
    push_trace(&run_result.stage_trace,
               OrchestratorStage::RecoveryRound,
               fsm->current_state(),
               fsm->current_state(),
               false,
               "skipped because direct response path did not require recovery");
  }

  const RuntimeState terminalize_before = fsm->current_state();
  StateTransitionOutcome completed_outcome;
  if (const auto failure = apply_steps(
          *fsm,
          orchestrator_stage_name(OrchestratorStage::Terminalize),
          {{.to_state = RuntimeState::Auditing,
            .reason = "response materialized",
            .guards = {TransitionGuardFact::ResponseMaterialized}},
           {.to_state = RuntimeState::Persisting,
            .reason = "audit committed",
            .guards = {TransitionGuardFact::AuditCommitted}},
           {.to_state = RuntimeState::Completed,
            .reason = "persistence confirmed",
            .guards = {TransitionGuardFact::PersistenceConfirmed}}},
          &completed_outcome);
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               terminalize_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator failed during terminalize",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto final_checkpoint = build_and_save_checkpoint(
      checkpoint_manager_,
      CheckpointBuildRequest{
          .transition_outcome = completed_outcome,
          .checkpoint_id = std::string("chk-complete-") + *normalized_request.request_id,
          .step_id = std::string("complete-") + *normalized_request.request_id,
          .working_memory_snapshot = std::string("wm:complete:") + *normalized_request.request_id,
          .pending_action = std::string(),
          .request_id = normalized_request.request_id,
          .goal_id = goal_id,
          .belief_state_ref = composition_.default_belief_state_ref,
          .retry_count = 0,
          .created_at_ms = normalized_request.created_at,
          .runtime_budget_snapshot = budget_controller_.snapshot(),
          .tags = {"path=success"},
      });
  if (!final_checkpoint.saved()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               terminalize_before,
               fsm->current_state(),
               true,
               final_checkpoint.detail);
    run_result.agent_result = make_result(
        normalized_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator failed to save completion checkpoint",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           final_checkpoint.detail,
                           orchestrator_stage_name(OrchestratorStage::Terminalize),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto persisted_session = persist_terminal_session(
      session_manager_,
      session_snapshot,
      RuntimeState::Completed,
      final_checkpoint.checkpoint->checkpoint_id,
      composition_.default_audit_summary);
  if (persisted_session.updated()) {
    run_result.effective_session = persisted_session.snapshot;
  }
  run_result.checkpoint = final_checkpoint.checkpoint;
  run_result.final_state = fsm->current_state();
  push_trace(&run_result.stage_trace,
             OrchestratorStage::Terminalize,
             terminalize_before,
             fsm->current_state(),
             true,
             "response audited, checkpointed, and persisted through real controllers");
  run_result.agent_result = make_result(
      normalized_request,
      run_result.final_state,
      contracts::AgentResultStatus::Completed,
      kRuntimeOrchestratorSkeletonCompletedCode,
      composition_.stub_ports.success_response_text,
      std::nullopt,
      final_checkpoint.checkpoint->checkpoint_id,
      goal_id);
  return run_result;
}

OrchestratorRunResult AgentOrchestrator::continue_from_checkpoint(
    const ResumePlan& plan,
    const SessionSnapshot& session_snapshot) {
  OrchestratorRunResult run_result;
  contracts::AgentRequest synthetic_request;
  synthetic_request.request_id = session_snapshot.request_id;
  synthetic_request.session_id = session_snapshot.session_id;
  synthetic_request.trace_id = plan.resume_token.empty()
                                   ? std::string("trace-resume-") + session_snapshot.session_id
                                   : std::string("trace-resume-") + plan.resume_token;
  synthetic_request.user_input = plan.resume_reason.empty()
                                     ? std::string("resume-from-checkpoint")
                                     : plan.resume_reason;
  synthetic_request.request_channel = contracts::RequestChannel::Cli;
  synthetic_request.created_at = current_time_ms();
  const auto goal_id = composition_.default_goal_id;

  const auto load_result = checkpoint_manager_.load(plan.checkpoint_ref);
  if (!load_result.loaded()) {
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator could not load resume checkpoint",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           load_result.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  auto fsm = build_fsm(plan.target_state);
  if (!fsm) {
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator cannot build resume FSM",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           "fsm factory returned null",
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

    const auto budget_decision = load_result.runtime_budget_snapshot.has_value()
                     ? budget_controller_.restore(
                       *load_result.runtime_budget_snapshot,
                       load_result.runtime_budget_snapshot->snapshot_at_ms
                         .value_or(composition_.budget_started_at_ms))
                     : budget_controller_.initialize(
                       BudgetInitializeRequest{
                         .runtime_budget = composition_.default_runtime_budget,
                         .started_at_ms = composition_.budget_started_at_ms,
                       });
  if (budget_decision.rejected()) {
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator failed to initialize resume budget",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           budget_decision.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  SessionSnapshot effective_session = session_snapshot;
  run_result.resume_plan = plan;
  push_trace(&run_result.stage_trace,
             OrchestratorStage::Preflight,
             fsm->current_state(),
             fsm->current_state(),
             true,
         load_result.runtime_budget_snapshot.has_value()
           ? "continue_from_checkpoint loaded checkpoint and restored budget snapshot"
           : "continue_from_checkpoint loaded checkpoint and initialized default budget");

  const auto resume_runtime_budget = load_result.runtime_budget_snapshot.has_value()
                                         ? runtime_budget_from_snapshot(
                                               *load_result.runtime_budget_snapshot)
                                         : std::optional<contracts::RuntimeBudget>(
                                               composition_.default_runtime_budget);
  if (!resume_runtime_budget.has_value()) {
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime orchestrator could not project the resume budget into a context request",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           "budget snapshot could not be converted back into runtime budget limits",
                           orchestrator_stage_name(OrchestratorStage::MainLoop),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto context_result = prepare_resume_context(
      composition_,
      synthetic_request,
      plan,
      goal_id,
      *resume_runtime_budget);
  if (context_result.result_code.has_value() || context_result.degraded) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::MainLoop,
               fsm->current_state(),
               fsm->current_state(),
               true,
               context_result.result_code.has_value()
                   ? "resume context assembly returned a failure result"
                   : "resume context assembly returned a degraded packet");
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "runtime resume path could not assemble a refreshed context packet",
        make_runtime_error(
            kRuntimeOrchestratorSkeletonInternalErrorCode,
            context_assembly_failure_detail(
                context_result,
                "memory context assembly did not produce a ready packet for resume"),
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const RuntimeState main_loop_before = fsm->current_state();
  if (plan.target_state == RuntimeState::Planning) {
    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            {{.to_state = RuntimeState::Reasoning,
              .reason = "resume planning after reflection recovery",
              .guards = {TransitionGuardFact::ContextAssembled}},
             {.to_state = RuntimeState::Responding,
              .reason = "resume direct response",
              .guards = {TransitionGuardFact::DirectResponseReady}}});
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          synthetic_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "continue_from_checkpoint failed to re-enter planning after reflection recovery",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
  } else if (plan.target_state == RuntimeState::WaitingClarify) {
    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            {{.to_state = RuntimeState::Receiving,
              .reason = "resume after clarification",
              .guards = {TransitionGuardFact::AgentRequestAvailable}},
             {.to_state = RuntimeState::Planning,
              .reason = "resume preflight complete",
              .guards = {TransitionGuardFact::RequestValidated,
                         TransitionGuardFact::SessionLoaded,
                         TransitionGuardFact::BudgetInitialized}},
             {.to_state = RuntimeState::Reasoning,
                      .reason = "resume context refreshed through memory manager",
              .guards = {TransitionGuardFact::ContextAssembled}},
             {.to_state = RuntimeState::Responding,
              .reason = "resume direct response",
              .guards = {TransitionGuardFact::DirectResponseReady}}});
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          synthetic_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "continue_from_checkpoint failed to re-enter main loop",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
  } else if (plan.target_state == RuntimeState::WaitingExternal) {
    if (const auto failure = apply_steps(
            *fsm,
            orchestrator_stage_name(OrchestratorStage::MainLoop),
            {{.to_state = RuntimeState::Reflecting,
              .reason = "external result replayed",
              .guards = {TransitionGuardFact::ExternalResultAvailable}},
             {.to_state = RuntimeState::Reasoning,
              .reason = "reflection continue path",
              .guards = {TransitionGuardFact::ReflectionContinue}},
             {.to_state = RuntimeState::Responding,
              .reason = "resume direct response",
              .guards = {TransitionGuardFact::DirectResponseReady}}});
        failure.has_value()) {
      push_trace(&run_result.stage_trace,
                 OrchestratorStage::MainLoop,
                 main_loop_before,
                 failure->state_before,
                 true,
                 failure->detail);
      run_result.agent_result = make_result(
          synthetic_request,
          RuntimeState::Failed,
          contracts::AgentResultStatus::Failed,
          kRuntimeOrchestratorSkeletonInternalErrorCode,
          "continue_from_checkpoint failed to resume waiting external state",
          make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                             failure->detail,
                             failure->stage,
                             RuntimeState::Failed),
          std::nullopt,
          goal_id);
      run_result.final_state = RuntimeState::Failed;
      return run_result;
    }
  }
  push_trace(&run_result.stage_trace,
             OrchestratorStage::MainLoop,
             main_loop_before,
             fsm->current_state(),
             true,
             "resume plan refreshed context and re-entered runtime-local response path");
  push_trace(&run_result.stage_trace,
             OrchestratorStage::ToolRound,
             fsm->current_state(),
             fsm->current_state(),
             false,
             "resume direct path does not require tool round");
  push_trace(&run_result.stage_trace,
             OrchestratorStage::RecoveryRound,
             fsm->current_state(),
             fsm->current_state(),
             false,
             "resume direct path does not require recovery round");

  const RuntimeState terminalize_before = fsm->current_state();
  StateTransitionOutcome completed_outcome;
  if (const auto failure = apply_steps(
          *fsm,
          orchestrator_stage_name(OrchestratorStage::Terminalize),
          {{.to_state = RuntimeState::Auditing,
            .reason = "response materialized",
            .guards = {TransitionGuardFact::ResponseMaterialized}},
           {.to_state = RuntimeState::Persisting,
            .reason = "audit committed",
            .guards = {TransitionGuardFact::AuditCommitted}},
           {.to_state = RuntimeState::Completed,
            .reason = "persistence confirmed",
            .guards = {TransitionGuardFact::PersistenceConfirmed}}},
          &completed_outcome);
      failure.has_value()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               terminalize_before,
               failure->state_before,
               true,
               failure->detail);
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "continue_from_checkpoint failed during terminalize",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           failure->detail,
                           failure->stage,
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto final_checkpoint = build_and_save_checkpoint(
      checkpoint_manager_,
      CheckpointBuildRequest{
          .transition_outcome = completed_outcome,
          .checkpoint_id = std::string("chk-resume-") + session_snapshot.session_id,
          .step_id = std::string("resume-") + session_snapshot.request_id,
          .working_memory_snapshot = std::string("wm:resume:") + session_snapshot.request_id,
          .pending_action = std::string(),
          .request_id = session_snapshot.request_id,
          .goal_id = goal_id,
          .belief_state_ref = composition_.default_belief_state_ref,
          .retry_count = 0,
          .created_at_ms = current_time_ms(),
          .runtime_budget_snapshot = budget_controller_.snapshot(),
          .tags = {"path=resume"},
      });
  if (!final_checkpoint.saved()) {
    push_trace(&run_result.stage_trace,
               OrchestratorStage::Terminalize,
               terminalize_before,
               fsm->current_state(),
               true,
               final_checkpoint.detail);
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "continue_from_checkpoint failed to save completion checkpoint",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           final_checkpoint.detail,
                           orchestrator_stage_name(OrchestratorStage::Terminalize),
                           RuntimeState::Failed),
        std::nullopt,
        goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto persisted_session = persist_terminal_session(
      session_manager_,
      effective_session,
      RuntimeState::Completed,
      final_checkpoint.checkpoint->checkpoint_id,
      composition_.default_audit_summary + " resume");
  if (persisted_session.updated()) {
    run_result.effective_session = persisted_session.snapshot;
  }
  run_result.checkpoint = final_checkpoint.checkpoint;
  run_result.final_state = fsm->current_state();
  push_trace(&run_result.stage_trace,
             OrchestratorStage::Terminalize,
             terminalize_before,
             fsm->current_state(),
             true,
             "resume path audited, checkpointed, and persisted");
  run_result.agent_result = make_result(
      synthetic_request,
      run_result.final_state,
      contracts::AgentResultStatus::Completed,
      kRuntimeOrchestratorSkeletonCompletedCode,
      composition_.stub_ports.success_response_text,
      std::nullopt,
      final_checkpoint.checkpoint->checkpoint_id,
      goal_id);
  return run_result;
}

OrchestratorRunResult AgentOrchestrator::handle_waiting_state(
    const SessionSnapshot& session_snapshot,
    const ResumeHandleRequest& request) {
  OrchestratorRunResult run_result;
  const auto load_result = session_manager_.load_session(
      SessionLoadRequest{
      .session_id = session_snapshot.session_id.empty() ? request.session_id
                              : session_snapshot.session_id,
      .request_id = session_snapshot.request_id.empty() ? request.request_id
                              : session_snapshot.request_id,
          .checkpoint_ref = request.checkpoint_ref,
          .allow_session_create = false,
      });
  if (!load_result.has_snapshot()) {
    contracts::AgentRequest synthetic_request;
    synthetic_request.request_id = request.request_id;
    synthetic_request.session_id = request.session_id;
    synthetic_request.trace_id = request.trace_context;
    synthetic_request.user_input = std::string("waiting-state-dispatch");
    synthetic_request.request_channel = contracts::RequestChannel::Cli;
    synthetic_request.created_at = current_time_ms();
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "handle_waiting_state failed to reload waiting session",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           load_result.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        composition_.default_goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  if (!load_result.snapshot->pending_interaction.has_value() ||
      !load_result.snapshot->pending_interaction->active() ||
      !load_result.snapshot->active_checkpoint_ref.has_value()) {
    contracts::AgentRequest synthetic_request;
    synthetic_request.request_id = request.request_id;
    synthetic_request.session_id = request.session_id;
    synthetic_request.trace_id = request.trace_context;
    synthetic_request.user_input = std::string("waiting-state-dispatch");
    synthetic_request.request_channel = contracts::RequestChannel::Cli;
    synthetic_request.created_at = current_time_ms();
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "handle_waiting_state requires an active waiting session snapshot",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           "waiting session snapshot is incomplete",
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        composition_.default_goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto prepare_result = session_manager_.prepare_turn(
      PrepareTurnRequest{
          .session_snapshot = *load_result.snapshot,
          .resume_turn = true,
          .expected_checkpoint_ref = request.checkpoint_ref,
      });
  if (!prepare_result.accepted || !prepare_result.effective_session.has_value()) {
    contracts::AgentRequest synthetic_request;
    synthetic_request.request_id = request.request_id;
    synthetic_request.session_id = request.session_id;
    synthetic_request.trace_id = request.trace_context;
    synthetic_request.user_input = std::string("waiting-state-dispatch");
    synthetic_request.request_channel = contracts::RequestChannel::Cli;
    synthetic_request.created_at = current_time_ms();
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "handle_waiting_state failed to prepare resume turn",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           prepare_result.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        composition_.default_goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto resume_seed_result = session_manager_.build_resume_seed(
      BuildResumeSeedRequest{
          .session_snapshot = *prepare_result.effective_session,
          .checkpoint_ref = request.checkpoint_ref,
        .resume_token = request.resume_token,
          .resume_reason = request.resume_reason,
          .policy_snapshot_ref = composition_.default_policy_snapshot_ref,
      });
  if (!resume_seed_result.built()) {
    contracts::AgentRequest synthetic_request;
    synthetic_request.request_id = request.request_id;
    synthetic_request.session_id = request.session_id;
    synthetic_request.trace_id = request.trace_context;
    synthetic_request.user_input = std::string("waiting-state-dispatch");
    synthetic_request.request_channel = contracts::RequestChannel::Cli;
    synthetic_request.created_at = current_time_ms();
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "handle_waiting_state failed to build resume seed",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           resume_seed_result.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        composition_.default_goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto checkpoint_load = checkpoint_manager_.load(request.checkpoint_ref);
  if (!checkpoint_load.loaded()) {
    contracts::AgentRequest synthetic_request;
    synthetic_request.request_id = request.request_id;
    synthetic_request.session_id = request.session_id;
    synthetic_request.trace_id = request.trace_context;
    synthetic_request.user_input = std::string("waiting-state-dispatch");
    synthetic_request.request_channel = contracts::RequestChannel::Cli;
    synthetic_request.created_at = current_time_ms();
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "handle_waiting_state failed to load resume checkpoint",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           checkpoint_load.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        composition_.default_goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  const auto resume_plan_decision = checkpoint_manager_.make_resume_plan(
      *checkpoint_load.checkpoint,
      *resume_seed_result.resume_seed);
  if (resume_plan_decision.rejected() || !resume_plan_decision.plan.has_value()) {
    contracts::AgentRequest synthetic_request;
    synthetic_request.request_id = request.request_id;
    synthetic_request.session_id = request.session_id;
    synthetic_request.trace_id = request.trace_context;
    synthetic_request.user_input = std::string("waiting-state-dispatch");
    synthetic_request.request_channel = contracts::RequestChannel::Cli;
    synthetic_request.created_at = current_time_ms();
    run_result.agent_result = make_result(
        synthetic_request,
        RuntimeState::Failed,
        contracts::AgentResultStatus::Failed,
        kRuntimeOrchestratorSkeletonInternalErrorCode,
        "handle_waiting_state failed to build resume plan",
        make_runtime_error(kRuntimeOrchestratorSkeletonInternalErrorCode,
                           resume_plan_decision.detail,
                           orchestrator_stage_name(OrchestratorStage::Preflight),
                           RuntimeState::Failed),
        std::nullopt,
        composition_.default_goal_id);
    run_result.final_state = RuntimeState::Failed;
    return run_result;
  }

  run_result = continue_from_checkpoint(*resume_plan_decision.plan, *prepare_result.effective_session);
  run_result.resume_plan = resume_plan_decision.plan;
  return run_result;
}

}  // namespace dasall::runtime

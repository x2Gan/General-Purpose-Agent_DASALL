#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "AgentOrchestrator.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "IMemoryManager.h"
#include "IToolManager.h"
#include "RuntimeDependencySet.h"
#include "RuntimePolicySnapshot.h"
#include "ToolInvocationEnvelope.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::AgentResultStatus;
using dasall::contracts::ObservationSource;
using dasall::runtime::AgentOrchestrator;
using dasall::runtime::OrchestratorComposition;
using dasall::runtime::StubMainLoopExit;
using dasall::tests::support::assert_true;

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] dasall::contracts::AgentRequest make_request(
    const std::string& request_id) {
  dasall::contracts::AgentRequest request;
  request.request_id = request_id;
  request.session_id = std::string{"session-017-policy"};
  request.trace_id = std::string{"trace-017-policy"};
  request.user_input = std::string{"exercise runtime policy consumers"};
  request.request_channel = dasall::contracts::RequestChannel::Cli;
  request.created_at = 1710000170000;
  return request;
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
make_policy_snapshot(const std::int64_t tool_timeout_ms,
                     const std::int64_t workflow_timeout_ms) {
  dasall::profiles::ModelProfile model_profile;
  model_profile.stage_routes.emplace(
      "planning",
      dasall::profiles::ModelRoutePolicy{
          .route = "desktop_full.planning.primary",
          .fallback_route = std::string{"desktop_full.planning.fallback"},
          .streaming_enabled = false,
      });
  model_profile.stage_routes.emplace(
      "execution",
      dasall::profiles::ModelRoutePolicy{
          .route = "desktop_full.execution.primary",
          .fallback_route = std::string{"desktop_full.execution.fallback"},
          .streaming_enabled = false,
      });
  model_profile.stage_routes.emplace(
      "reflection",
      dasall::profiles::ModelRoutePolicy{
          .route = "desktop_full.reflection.primary",
          .fallback_route = std::string{"desktop_full.reflection.fallback"},
          .streaming_enabled = false,
      });
  model_profile.stage_routes.emplace(
      "response",
      dasall::profiles::ModelRoutePolicy{
          .route = "desktop_full.response.primary",
          .fallback_route = std::string{"desktop_full.response.fallback"},
          .streaming_enabled = true,
      });

  return std::make_shared<const dasall::profiles::RuntimePolicySnapshot>(
      17U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = 3U,
          .max_latency_ms = 6000U,
          .max_replan_count = 2U,
      },
      std::move(model_profile),
      dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 2048U,
          .max_output_tokens = 512U,
          .max_history_turns = 8U,
          .compression_threshold = 1024U,
      },
      dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"runtime"},
          .tool_visibility_rules = {"builtin:agent.dataset"},
      },
      dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 5000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 100,
      },
      dasall::profiles::DegradePolicy{
          .fallback_chain = {"safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 2U,
          },
          .tool = dasall::profiles::TimeoutBudget{
              .timeout_ms = tool_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 2U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
              .timeout_ms = 2000,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 2U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
              .timeout_ms = workflow_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 2U,
          },
      },
      dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
      },
      dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "detailed",
          .trace_sample_ratio = 1.0,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      2U);
}

class StaticMemoryManager final : public dasall::memory::IMemoryManager {
 public:
  dasall::contracts::ResultCode init(const dasall::memory::MemoryConfig&) override {
    return static_cast<dasall::contracts::ResultCode>(0);
  }

  void shutdown() noexcept override {}

  [[nodiscard]] dasall::memory::ContextAssemblyResult prepare_context(
      const dasall::memory::MemoryContextRequest& request) override {
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
      const dasall::memory::MemoryWritebackRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::WorkingMemoryExportResult export_working_memory_snapshot(
      const dasall::memory::WorkingMemoryExportRequest&) override {
    return {};
  }

  [[nodiscard]] dasall::memory::MaintenanceReport run_maintenance(
      const dasall::memory::MaintenanceRequest&) override {
    return {};
  }
};

class ToolRoundCognitionEngine final : public dasall::cognition::ICognitionEngine {
 public:
  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest&) override {
    dasall::cognition::CognitionDecisionResult result;

    dasall::cognition::decision::ActionDecision decision;
    decision.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::ExecuteAction;
    decision.selected_node_id = std::string{"runtime-policy-consumer-node"};
    decision.rationale = std::string{"runtime should consume policy through tool timeout view"};
    decision.confidence = 0.93F;
    decision.tool_intent_hint = dasall::cognition::decision::ToolIntentHint{
        .tool_name = std::string{"agent.dataset"},
        .intent_summary = std::string{"capture tool timeout and idempotency policy binding"},
        .argument_hints = {std::string{"policy consumer integration"}},
        .evidence_refs = {std::string{"tests:runtime-policy-consumer"}},
    };
    decision.response_outline = dasall::cognition::decision::ResponseOutline{
        .summary = std::string{"runtime policy consumer integration completed"},
        .key_points = {std::string{"tool timeout and workflow deadline remain runtime-owned"}},
    };

    result.action_decision = std::move(decision);
    result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = true,
        .context_confidence = 1.0F,
        .missing_evidence_hints = {},
        .recommend_context_reload = false,
    };
    return result;
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest&) override {
    return {};
  }
};

class StaticResponseBuilder final : public dasall::cognition::IResponseBuilder {
 public:
  [[nodiscard]] dasall::cognition::ResponseBuildResult build(
      const dasall::cognition::ResponseBuildRequest& request) override {
    dasall::cognition::ResponseBuildResult result;
    result.agent_result = dasall::contracts::AgentResult{
        .result_id = request.request_id + std::string{"-policy-consumer"},
        .status = AgentResultStatus::Completed,
        .result_code = 0,
        .response_text = std::string{"runtime policy consumer integration completed"},
        .task_completed = true,
        .created_at = current_time_ms(),
        .request_id = request.request_id,
        .trace_id = request.trace_id,
        .structured_payload = std::string{"{\"result\":\"ok\"}"},
        .error_info = std::nullopt,
        .checkpoint_ref = std::nullopt,
        .goal_id = request.goal_contract.goal_id,
        .tags = std::vector<std::string>{"integration", "runtime_policy_consumer"},
    };
    return result;
  }
};

class RecordingToolManager final : public dasall::tools::IToolManager {
 public:
  [[nodiscard]] dasall::tools::ToolInvocationEnvelope invoke(
      const dasall::contracts::ToolRequest& request,
      const dasall::tools::ToolInvocationContext&) override {
    last_request = request;

    const auto completed_at = current_time_ms();
    const auto tool_call_id = request.tool_call_id.value_or(std::string{"tool-call-missing"});
    const auto payload = std::string{"{\"items\":[]}"};

    return dasall::tools::ToolInvocationEnvelope{
        .tool_result = dasall::contracts::ToolResult{
            .request_id = request.request_id,
            .tool_call_id = request.tool_call_id,
            .tool_name = request.tool_name,
            .success = true,
            .payload = payload,
            .error = std::nullopt,
            .side_effects = std::nullopt,
            .completed_at = completed_at,
            .duration_ms = 12,
            .goal_id = request.goal_id,
            .worker_task_id = request.worker_task_id,
            .tags = std::vector<std::string>{"integration", "runtime_policy_consumer"},
        },
        .observation = dasall::contracts::Observation{
            .observation_id = std::string{"obs-runtime-policy-consumer"},
            .source = ObservationSource::ToolExecution,
            .success = true,
            .payload = payload,
            .created_at = completed_at,
            .error = std::nullopt,
            .side_effects = std::nullopt,
            .tool_call_id = tool_call_id,
            .worker_task_id = request.worker_task_id,
            .request_id = request.request_id,
            .goal_id = request.goal_id,
            .duration_ms = 12,
            .tags = std::vector<std::string>{"integration", "runtime_policy_consumer"},
        },
        .observation_digest = dasall::contracts::ObservationDigest{
            .observation_id = std::string{"obs-runtime-policy-consumer"},
            .summary = std::string{"tool invocation completed"},
            .key_facts = std::vector<std::string>{"runtime captured tool timeout from policy"},
            .citations = std::vector<std::string>{tool_call_id},
            .confidence = 1.0F,
            .omitted_details = std::vector<std::string>{},
            .source = ObservationSource::ToolExecution,
            .created_at = completed_at,
            .tags = std::vector<std::string>{"integration", "runtime_policy_consumer"},
        },
        .route_facts = std::nullopt,
        .evidence_refs = std::nullopt,
        .compensation_hints = std::nullopt,
        .failure_reason_code = std::nullopt,
    };
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

  std::optional<dasall::contracts::ToolRequest> last_request;
};

void test_runtime_policy_consumer_integration_binds_workflow_deadline() {
  OrchestratorComposition composition;
  composition.runtime_instance_id = "rt-017-policy-wait";
  composition.profile_id = "desktop_full";
  composition.policy_snapshot = make_policy_snapshot(2100, 4321);
  composition.dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
  composition.stub_ports.main_loop_exit = StubMainLoopExit::WaitingClarify;

  AgentOrchestrator orchestrator(std::move(composition));
  const auto lower_bound = current_time_ms();
  const auto result = orchestrator.run_once(make_request("req-017-policy-wait"));
  const auto upper_bound = current_time_ms();

  assert_true(result.agent_result.status == AgentResultStatus::PartiallyCompleted,
              "waiting clarify path should return a resumable partial result");
  assert_true(result.effective_session.has_value() &&
                  result.effective_session->pending_interaction.has_value() &&
                  result.effective_session->pending_interaction->deadline_ms.has_value(),
              "waiting clarify path should bind pending interaction deadline into the session");

  const auto deadline_ms =
      *result.effective_session->pending_interaction->deadline_ms;
  assert_true(deadline_ms >= lower_bound + 4321 && deadline_ms <= upper_bound + 4321,
              "waiting clarify deadline should bind workflow timeout from RuntimePolicySnapshot");
}

void test_runtime_policy_consumer_integration_binds_tool_timeout_and_idempotency() {
  auto dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
  dependency_set->memory_manager = std::make_shared<StaticMemoryManager>();
  dependency_set->cognition_engine = std::make_shared<ToolRoundCognitionEngine>();
  dependency_set->response_builder = std::make_shared<StaticResponseBuilder>();
  auto tool_manager = std::make_shared<RecordingToolManager>();
  dependency_set->tool_manager = tool_manager;
  dependency_set->visible_tools = {"agent.dataset"};
  dependency_set->external_evidence = {"tests:runtime-policy-consumer"};

  OrchestratorComposition composition;
  composition.runtime_instance_id = "rt-017-policy-tool";
  composition.profile_id = "desktop_full";
  composition.policy_snapshot = make_policy_snapshot(3210, 4321);
  composition.dependency_set = dependency_set;

  AgentOrchestrator orchestrator(std::move(composition));
  const auto result = orchestrator.run_once(make_request("req-017-policy-tool"));

  assert_true(result.agent_result.status == AgentResultStatus::Completed,
              "live tool path should converge to a completed AgentResult");
  assert_true(tool_manager->last_request.has_value(),
              "live tool path should dispatch one concrete ToolRequest");
  assert_true(tool_manager->last_request->timeout_ms.has_value() &&
                  *tool_manager->last_request->timeout_ms == 3210U,
              "tool request timeout should consume timeout_policy.tool.timeout_ms");
  assert_true(tool_manager->last_request->tool_call_id.has_value() &&
                  tool_manager->last_request->idempotency_key ==
                      tool_manager->last_request->tool_call_id,
              "tool request idempotency should reuse the runtime-owned tool call identity");
}

}  // namespace

int main() {
  try {
    test_runtime_policy_consumer_integration_binds_workflow_deadline();
    test_runtime_policy_consumer_integration_binds_tool_timeout_and_idempotency();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
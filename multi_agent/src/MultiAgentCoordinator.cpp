#include "IMultiAgentCoordinator.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "execution/CompensationLedger.h"
#include "observation/ObservationSource.h"

namespace dasall::multi_agent {
namespace {

constexpr int kMultiAgentFailureCode = 18018;

[[nodiscard]] std::string select_string(const std::optional<std::string>& value,
                                        std::string fallback) {
  if (value.has_value() && !value->empty()) {
    return *value;
  }
  return fallback;
}

[[nodiscard]] std::string build_worker_task_id(const contracts::MultiAgentRequest& request) {
  return std::string("worker:") +
         select_string(request.parent_task_id, std::string("multi-agent-parent"));
}

[[nodiscard]] std::string build_graph_id(const contracts::MultiAgentRequest& request) {
  return std::string("graph:") +
         select_string(request.parent_task_id, std::string("multi-agent-parent"));
}

[[nodiscard]] std::vector<std::string> collect_side_effects(
    const MultiAgentExecutionContext& context) {
  std::vector<std::string> side_effects;
  for (const auto& tool_result : context.tool_results) {
    if (!tool_result.side_effects.has_value()) {
      continue;
    }
    for (const auto& side_effect : *tool_result.side_effects) {
      if (!side_effect.empty()) {
        side_effects.push_back(side_effect);
      }
    }
  }
  return side_effects;
}

[[nodiscard]] std::vector<tools::ToolCompensationHint> build_compensation_hints(
    const contracts::MultiAgentRequest& request,
    const MultiAgentExecutionContext& context) {
  tools::execution::CompensationLedger ledger;
  const auto worker_task_id = build_worker_task_id(request);
  for (std::size_t index = 0; index < context.tool_results.size(); ++index) {
    ledger.register_result(
        worker_task_id + ":tool:" + std::to_string(index + 1U),
        "multi_agent.loopback",
        context.tool_results[index],
        context.tool_results[index].success.value_or(false));
  }
  return ledger.build_hints();
}

[[nodiscard]] std::optional<std::string> build_failure_summary(
    const MultiAgentExecutionContext& context) {
  if (context.latest_observation.has_value() &&
      !context.latest_observation->success.value_or(true)) {
    if (context.latest_observation->error.has_value() &&
        !context.latest_observation->error->details.message.empty()) {
      return context.latest_observation->error->details.message;
    }
    return std::string("multi_agent received a failed upstream observation");
  }

  for (const auto& tool_result : context.tool_results) {
    if (!tool_result.success.value_or(true)) {
      if (tool_result.error.has_value() && !tool_result.error->details.message.empty()) {
        return tool_result.error->details.message;
      }
      return std::string("multi_agent received a failed tool result");
    }
  }

  return std::nullopt;
}

[[nodiscard]] contracts::ErrorInfo build_error_info(
    const std::string& worker_task_id,
    const std::string& message,
    const bool replay_safe) {
  return contracts::ErrorInfo{
      .failure_type = contracts::ResultCodeCategory::Runtime,
      .retryable = replay_safe,
      .safe_to_replan = true,
      .details = contracts::ErrorDetails{
          .code = kMultiAgentFailureCode,
          .message = message,
          .stage = std::string("multi_agent.coordinate"),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = std::string("worker_task"),
          .ref_id = worker_task_id,
      },
  };
}

[[nodiscard]] contracts::Observation build_observation(
    const contracts::MultiAgentRequest& request,
    const MultiAgentExecutionContext& context,
    const contracts::MultiAgentResult& multi_agent_result,
    const std::vector<tools::ToolCompensationHint>& compensation_hints,
    const std::optional<std::string>& failure_summary) {
  const auto worker_task_id = build_worker_task_id(request);
  const bool success = !failure_summary.has_value();
  contracts::Observation observation;
  observation.observation_id = std::string("observation:multi-agent:") + worker_task_id;
  observation.source = contracts::ObservationSource::WorkerAgent;
  observation.success = success;
  observation.payload = multi_agent_result.merged_result;
  observation.created_at = context.latest_observation.has_value()
                               ? context.latest_observation->created_at.value_or(1)
                               : 1;
  observation.worker_task_id = worker_task_id;
  observation.request_id = context.request_id.has_value()
                               ? context.request_id
                               : request.parent_request_id;
  observation.goal_id = context.goal_id.has_value()
                            ? context.goal_id
                            : request.goal_fragment;

  const auto side_effects = collect_side_effects(context);
  if (!side_effects.empty()) {
    observation.side_effects = side_effects;
  }

  std::vector<std::string> tags{"multi_agent", "loopback"};
  if (!success) {
    tags.push_back("recovery-advisory");
    observation.error = build_error_info(
        worker_task_id,
        *failure_summary,
        compensation_hints.empty());
  } else {
    tags.push_back("folded");
  }
  observation.tags = std::move(tags);
  return observation;
}

[[nodiscard]] contracts::ReflectionDecisionKind map_recovery_action(
    const std::string& recommended_next_action) {
  if (recommended_next_action == "abort_safe") {
    return contracts::ReflectionDecisionKind::AbortSafe;
  }
  if (recommended_next_action == "replan") {
    return contracts::ReflectionDecisionKind::Replan;
  }
  if (recommended_next_action == "continue") {
    return contracts::ReflectionDecisionKind::Continue;
  }
  return contracts::ReflectionDecisionKind::RetryStep;
}

[[nodiscard]] std::optional<contracts::RecoveryRequest> build_recovery_request(
    const contracts::MultiAgentRequest& request,
    const MultiAgentExecutionContext& context,
    const contracts::MultiAgentResult& multi_agent_result,
    const contracts::Observation& observation,
    const std::vector<tools::ToolCompensationHint>& compensation_hints,
    const std::optional<std::string>& failure_summary) {
  if (!context.checkpoint.has_value() || !failure_summary.has_value() ||
      !multi_agent_result.recommended_next_action.has_value()) {
    return std::nullopt;
  }

  const bool compensation_pending = !compensation_hints.empty();
  return contracts::RecoveryRequest{
      .reflection_decision = contracts::ReflectionDecision{
          .request_id = context.request_id.has_value() ? context.request_id : request.parent_request_id,
          .decision_kind = map_recovery_action(*multi_agent_result.recommended_next_action),
          .rationale = *failure_summary,
          .goal_id = context.goal_id.has_value() ? context.goal_id : request.goal_fragment,
        .confidence = std::nullopt,
          .relevant_observation_refs = std::vector<std::string>{
              observation.observation_id.value_or(std::string("observation:multi-agent-missing"))},
        .hint_ref = std::nullopt,
          .created_at = observation.created_at,
          .tags = std::vector<std::string>{"multi_agent", "runtime-fold"},
      },
      .error_info = observation.error,
      .latest_observation = observation,
      .checkpoint = context.checkpoint,
      .idempotency_and_side_effect_report = contracts::IdempotencyAndSideEffectReport{
          .replay_safe = !compensation_pending,
          .idempotency_key = std::string("multi-agent:") +
                             select_string(observation.worker_task_id,
                                           std::string("worker-task-missing")),
          .side_effects_present = observation.side_effects.has_value() &&
                                  !observation.side_effects->empty(),
          .non_replayable_reason = compensation_pending
                                       ? std::optional<std::string>(
                                             std::string("compensation hints pending before replay"))
                                       : std::nullopt,
      },
      .retry_count = context.retry_count,
      .runtime_budget_snapshot = context.runtime_budget_snapshot,
  };
}

class NullMultiAgentCoordinator final : public IMultiAgentCoordinator {
 public:
  [[nodiscard]] bool enabled() const override {
    return false;
  }

  [[nodiscard]] MultiAgentExecutionReport coordinate(
      const contracts::MultiAgentRequest& request,
      const MultiAgentExecutionContext&) const override {
    MultiAgentExecutionReport report;
    report.disabled = true;
    report.multi_agent_result = contracts::MultiAgentResult{
        .subtask_results = std::vector<std::string>{"multi_agent.disabled"},
        .merged_result = request.goal_fragment.has_value()
                             ? request.goal_fragment
                             : request.plan_fragment,
        .recommended_next_action = std::string("continue_single_agent"),
        .conflicts = std::nullopt,
        .worker_trace_refs = std::nullopt,
        .failure_summary = std::nullopt,
    };
    report.graph_snapshot = contracts::SubTaskGraph{
        .graph_id = build_graph_id(request),
        .root_task_id = request.parent_task_id,
        .task_ids = std::vector<std::string>{build_worker_task_id(request)},
        .graph_revision = 1U,
    };
    report.audit_refs = {
        std::string("multi_agent.disabled"),
        std::string("multi_agent.graph.") + build_graph_id(request),
    };
    return report;
  }
};

class MultiAgentCoordinator final : public IMultiAgentCoordinator {
 public:
  [[nodiscard]] bool enabled() const override {
    return true;
  }

  [[nodiscard]] MultiAgentExecutionReport coordinate(
      const contracts::MultiAgentRequest& request,
      const MultiAgentExecutionContext& context) const override {
    MultiAgentExecutionReport report;
    report.disabled = false;

    const auto compensation_hints = build_compensation_hints(request, context);
    const auto failure_summary = build_failure_summary(context);
    const auto merged_result = request.plan_fragment.has_value()
                                   ? request.plan_fragment
                                   : request.goal_fragment;
    const auto recommended_next_action = failure_summary.has_value()
                                             ? (compensation_hints.empty()
                                                    ? std::string("retry_step")
                                                    : std::string("abort_safe"))
                                             : std::string("continue");

    report.multi_agent_result = contracts::MultiAgentResult{
        .subtask_results = std::vector<std::string>{
            select_string(request.plan_fragment,
                          select_string(request.goal_fragment,
                                        std::string("multi_agent.loopback")))},
        .merged_result = merged_result,
        .recommended_next_action = recommended_next_action,
        .conflicts = failure_summary.has_value() && !compensation_hints.empty()
                         ? std::optional<std::vector<std::string>>(
                               std::vector<std::string>{"tool compensation pending"})
                         : std::nullopt,
        .worker_trace_refs = context.trace_id.has_value()
                                 ? std::optional<std::vector<std::string>>(
                                       std::vector<std::string>{
                                           std::string("trace:") + *context.trace_id})
                                 : std::nullopt,
        .failure_summary = failure_summary,
    };

    report.graph_snapshot = contracts::SubTaskGraph{
        .graph_id = build_graph_id(request),
        .root_task_id = request.parent_task_id,
        .task_ids = std::vector<std::string>{build_worker_task_id(request)},
        .graph_revision = 1U,
    };
    report.compensation_hints = compensation_hints;

    const auto observation = build_observation(
        request,
        context,
        *report.multi_agent_result,
        report.compensation_hints,
        failure_summary);
    report.emitted_observations.push_back(observation);
    report.recovery_request = build_recovery_request(
        request,
        context,
        *report.multi_agent_result,
        observation,
        report.compensation_hints,
        failure_summary);
    report.audit_refs = {
        std::string("multi_agent.enabled"),
        std::string("multi_agent.graph.") + build_graph_id(request),
        std::string("multi_agent.worker.") + build_worker_task_id(request),
    };
    return report;
  }
};

}  // namespace

std::shared_ptr<IMultiAgentCoordinator> create_multi_agent_coordinator(const bool enabled) {
  if (!enabled) {
    return create_null_multi_agent_coordinator();
  }
  return std::make_shared<MultiAgentCoordinator>();
}

std::shared_ptr<IMultiAgentCoordinator> create_null_multi_agent_coordinator() {
  return std::make_shared<NullMultiAgentCoordinator>();
}

}  // namespace dasall::multi_agent
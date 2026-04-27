#include "planning/PlanGraphBuilder.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dasall::cognition::planning {
namespace {

using plan::PlanEdge;
using plan::PlanGraph;
using plan::PlanNode;
using plan::PlanOpenQuestion;
using plan::ReplanResult;

constexpr float kReplanConfidence = 0.62F;
constexpr float kSteadyPlanConfidence = 0.78F;

template <typename T>
[[nodiscard]] const std::vector<T>& optional_vector_or_empty(
    const std::optional<std::vector<T>>& value) {
  static const std::vector<T> kEmpty;
  return value.has_value() ? *value : kEmpty;
}

[[nodiscard]] std::string optional_string_or(
    const std::optional<std::string>& value,
    std::string_view fallback) {
  return value.has_value() ? *value : std::string(fallback);
}

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto is_not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), is_not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(),
              value.end());
  return value;
}

[[nodiscard]] std::string sanitize_identifier(std::string_view raw) {
  std::string sanitized;
  sanitized.reserve(raw.size());

  bool previous_dash = false;
  for (const unsigned char ch : raw) {
    if (std::isalnum(ch)) {
      sanitized.push_back(static_cast<char>(std::tolower(ch)));
      previous_dash = false;
      continue;
    }

    if (!previous_dash) {
      sanitized.push_back('-');
      previous_dash = true;
    }
  }

  sanitized = trim_copy(std::move(sanitized));
  while (!sanitized.empty() && sanitized.front() == '-') {
    sanitized.erase(sanitized.begin());
  }
  while (!sanitized.empty() && sanitized.back() == '-') {
    sanitized.pop_back();
  }

  if (sanitized.empty()) {
    return std::string("node");
  }

  return sanitized;
}

[[nodiscard]] std::string make_plan_id(std::string_view request_id) {
  return std::string("plan-") + sanitize_identifier(request_id);
}

[[nodiscard]] std::string make_node_id(std::string_view plan_id,
                                       std::string_view suffix) {
  return sanitize_identifier(std::string(plan_id) + "-" + std::string(suffix));
}

void append_unique(std::vector<std::string>& target,
                   const std::vector<std::string>& source) {
  std::unordered_set<std::string> seen(target.begin(), target.end());
  for (const auto& value : source) {
    if (!value.empty() && seen.insert(value).second) {
      target.push_back(value);
    }
  }
}

[[nodiscard]] std::vector<std::string> collect_evidence_refs(
    const PlanningRequest& request) {
  std::vector<std::string> evidence_refs;
  append_unique(evidence_refs,
                optional_vector_or_empty(request.belief_state.evidence_refs));
  append_unique(evidence_refs,
                optional_vector_or_empty(request.context_packet.retrieval_evidence));

  for (const auto& entity : request.perception_result.entities) {
    append_unique(evidence_refs, entity.evidence_refs);
  }

  for (const auto& ambiguity : request.perception_result.ambiguities) {
    append_unique(evidence_refs, ambiguity.missing_evidence_refs);
  }

  return evidence_refs;
}

[[nodiscard]] std::string first_tool_hint(
    const perception::PerceptionResult& perception_result) {
  for (const auto& entity : perception_result.entities) {
    if (entity.name == "tool" && !entity.value.empty()) {
      return entity.value;
    }
  }

  return {};
}

[[nodiscard]] std::string derive_goal_summary(const PlanningRequest& request) {
  if (!request.perception_result.intent_summary.empty()) {
    return request.perception_result.intent_summary;
  }

  if (request.goal_contract.goal_description.has_value() &&
      !request.goal_contract.goal_description->empty()) {
    return *request.goal_contract.goal_description;
  }

  if (request.context_packet.current_goal_summary.has_value() &&
      !request.context_packet.current_goal_summary->empty()) {
    return *request.context_packet.current_goal_summary;
  }

  return std::string("advance the current goal safely");
}

[[nodiscard]] std::string derive_success_signal(const PlanningRequest& request) {
  if (request.goal_contract.success_criteria.has_value() &&
      !request.goal_contract.success_criteria->empty()) {
    return *request.goal_contract.success_criteria;
  }

  return std::string("goal completion validated against available evidence");
}

[[nodiscard]] std::string derive_primary_action_kind(
    const PlanningRequest& request) {
  if (request.perception_result.task_type == "direct_response") {
    return std::string("direct_response");
  }

  if (!first_tool_hint(request.perception_result).empty()) {
    return std::string("tool_execution");
  }

  if (request.perception_result.task_type == "action_decision") {
    return std::string("action_decision");
  }

  return std::string("analysis");
}

[[nodiscard]] std::vector<PlanEdge> build_edges_from_nodes(
    std::vector<PlanNode>& nodes) {
  std::vector<PlanEdge> edges;
  if (nodes.size() < 2U) {
    return edges;
  }

  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (index == 0U) {
      nodes[index].depends_on.clear();
      continue;
    }

    nodes[index].depends_on = {nodes[index - 1U].node_id};
    edges.push_back(PlanEdge{.from_node_id = nodes[index - 1U].node_id,
                             .to_node_id = nodes[index].node_id,
                             .condition = "on_success",
                             .evidence_refs = nodes[index].evidence_refs});
  }

  return edges;
}

[[nodiscard]] std::size_t longest_path_depth(const PlanGraph& graph) {
  if (graph.nodes.empty()) {
    return 0U;
  }

  std::unordered_map<std::string, std::size_t> indegree;
  std::unordered_map<std::string, std::vector<std::string>> adjacency;
  std::unordered_map<std::string, std::size_t> depth;
  indegree.reserve(graph.nodes.size());
  adjacency.reserve(graph.nodes.size());
  depth.reserve(graph.nodes.size());

  for (const auto& node : graph.nodes) {
    indegree[node.node_id] = 0U;
    depth[node.node_id] = 1U;
  }

  for (const auto& edge : graph.edges) {
    adjacency[edge.from_node_id].push_back(edge.to_node_id);
    ++indegree[edge.to_node_id];
  }

  std::vector<std::string> ready;
  ready.reserve(graph.nodes.size());
  for (const auto& [node_id, count] : indegree) {
    if (count == 0U) {
      ready.push_back(node_id);
    }
  }

  std::size_t processed = 0U;
  std::size_t max_depth = 1U;
  while (!ready.empty()) {
    const auto current = ready.back();
    ready.pop_back();
    ++processed;

    for (const auto& next : adjacency[current]) {
      depth[next] = std::max(depth[next], depth[current] + 1U);
      max_depth = std::max(max_depth, depth[next]);
      auto& remaining = indegree[next];
      --remaining;
      if (remaining == 0U) {
        ready.push_back(next);
      }
    }
  }

  if (processed != graph.nodes.size()) {
    return graph.nodes.size() + 1U;
  }

  return max_depth;
}

[[nodiscard]] bool all_nodes_have_required_fields(const PlanGraph& graph) {
  std::unordered_set<std::string> node_ids;
  node_ids.reserve(graph.nodes.size());

  for (const auto& node : graph.nodes) {
    if (node.node_id.empty() || node.objective.empty() ||
        node.success_signal.empty() || node.action_kind_hint.empty()) {
      return false;
    }
    if (!node_ids.insert(node.node_id).second) {
      return false;
    }
  }

  for (const auto& node : graph.nodes) {
    for (const auto& dependency : node.depends_on) {
      if (dependency.empty() || node_ids.count(dependency) == 0U ||
          dependency == node.node_id) {
        return false;
      }
    }
  }

  for (const auto& edge : graph.edges) {
    if (edge.from_node_id.empty() || edge.to_node_id.empty() ||
        edge.from_node_id == edge.to_node_id ||
        node_ids.count(edge.from_node_id) == 0U ||
        node_ids.count(edge.to_node_id) == 0U) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] PlanNode merge_nodes(const std::vector<PlanNode>& nodes,
                                   std::string node_id,
                                   std::string success_signal_override,
                                   std::string action_kind_override) {
  PlanNode merged;
  merged.node_id = std::move(node_id);

  std::ostringstream objective;
  std::vector<std::string> evidence_refs;
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    if (index > 0U) {
      objective << " -> ";
    }
    objective << nodes[index].objective;
    append_unique(evidence_refs, nodes[index].evidence_refs);
  }

  merged.objective = objective.str();
  merged.success_signal = success_signal_override.empty()
                              ? nodes.back().success_signal
                              : std::move(success_signal_override);
  merged.action_kind_hint = action_kind_override.empty()
                                ? nodes.back().action_kind_hint
                                : std::move(action_kind_override);
  merged.evidence_refs = std::move(evidence_refs);
  return merged;
}

[[nodiscard]] std::string derive_replan_reason(const ReplanRequest& request) {
  if (request.latest_observation.error.has_value() &&
      !request.latest_observation.error->details.message.empty()) {
    return std::string("observation_failure: ") +
           request.latest_observation.error->details.message;
  }

  if (request.latest_observation.payload.has_value() &&
      !request.latest_observation.payload->empty()) {
    return std::string("observation_update: ") + *request.latest_observation.payload;
  }

  return request.latest_observation.success.value_or(false)
             ? std::string("observation_update")
             : std::string("observation_failure");
}

[[nodiscard]] std::string derive_recovery_objective(const ReplanRequest& request) {
  if (request.latest_observation.error.has_value() &&
      !request.latest_observation.error->details.message.empty()) {
    return std::string("Recover from failure: ") +
           request.latest_observation.error->details.message;
  }

  if (request.latest_observation.payload.has_value() &&
      !request.latest_observation.payload->empty()) {
    return std::string("Integrate observation payload: ") +
           *request.latest_observation.payload;
  }

  return optional_string_or(request.goal_contract.goal_description,
                            "re-stabilize the current goal");
}

[[nodiscard]] std::vector<std::string> collect_replan_evidence_refs(
    const ReplanRequest& request) {
  std::vector<std::string> evidence_refs =
      optional_vector_or_empty(request.belief_state.evidence_refs);

  if (request.latest_observation.observation_id.has_value() &&
      !request.latest_observation.observation_id->empty()) {
    append_unique(evidence_refs,
                  std::vector<std::string>{*request.latest_observation.observation_id});
  }

  return evidence_refs;
}

}  // namespace

PlanGraphBuilder::PlanGraphBuilder(CognitionConfig config)
    : config_(std::move(config)) {}

PlanBuildLimits PlanGraphBuilder::derive_limits(
    const std::optional<BudgetContext>& budget_context) const {
  PlanBuildLimits limits{.max_plan_nodes = std::max(1U, config_.max_plan_nodes),
                         .max_plan_depth = std::max(1U, config_.max_plan_depth),
                         .degraded_mode = false};

  if (!budget_context.has_value()) {
    return limits;
  }

  if (budget_context->budget_utilization >= 0.8F) {
    limits.max_plan_nodes = std::min<std::uint32_t>(limits.max_plan_nodes, 2U);
    limits.max_plan_depth = std::min<std::uint32_t>(limits.max_plan_depth, 2U);
    limits.degraded_mode = true;
  } else if (budget_context->budget_utilization >= 0.5F) {
    limits.max_plan_nodes = std::max<std::uint32_t>(1U, limits.max_plan_nodes / 2U);
    limits.degraded_mode = true;
  }

  if (budget_context->near_budget_limit) {
    limits.max_plan_nodes = std::min<std::uint32_t>(limits.max_plan_nodes, 2U);
    limits.max_plan_depth = std::min<std::uint32_t>(limits.max_plan_depth, 2U);
    limits.degraded_mode = true;
  }

  return limits;
}

PlanGraph PlanGraphBuilder::build_clarification_plan(
    const PlanningRequest& request,
    const PlanBuildLimits&) const {
  PlanGraph graph;
  graph.plan_id = make_plan_id(request.request_id);
  graph.revision = 1U;
  graph.plan_rationale =
      "clarification required before plan graph expansion can proceed";

  if (!request.perception_result.clarification_questions.empty()) {
    for (std::size_t index = 0;
         index < request.perception_result.clarification_questions.size(); ++index) {
      const auto& candidate = request.perception_result.clarification_questions[index];
      const auto reason = index < request.perception_result.ambiguities.size()
                              ? request.perception_result.ambiguities[index].description
                              : std::string("missing information blocks plan expansion");
      graph.open_questions.push_back(PlanOpenQuestion{
          .question_id = make_node_id(graph.plan_id,
                                      std::string("open-question-") +
                                          std::to_string(index + 1U)),
          .question = candidate.question,
          .reason = reason,
          .blocks_plan = true,
          .evidence_refs = candidate.evidence_refs,
      });
    }
  } else {
    graph.open_questions.push_back(PlanOpenQuestion{
        .question_id = make_node_id(graph.plan_id, "open-question-1"),
        .question = "What is the missing detail needed to continue this task safely?",
        .reason = "perception marked the request as requiring clarification",
        .blocks_plan = true,
        .evidence_refs = collect_evidence_refs(request),
    });
  }

  graph.estimated_complexity =
      static_cast<std::uint32_t>(graph.open_questions.size());
  return graph;
}

PlanGraph PlanGraphBuilder::build_direct_response_plan(
    const PlanningRequest& request,
    const PlanBuildLimits&) const {
  PlanGraph graph;
  graph.plan_id = make_plan_id(request.request_id);
  graph.revision = 1U;
  graph.nodes.push_back(PlanNode{
      .node_id = make_node_id(graph.plan_id, "respond"),
      .objective = std::string("Deliver a direct response for: ") +
                   derive_goal_summary(request),
      .success_signal = derive_success_signal(request),
      .action_kind_hint = "direct_response",
      .depends_on = {},
      .evidence_refs = collect_evidence_refs(request),
  });
  graph.plan_rationale =
      "perception indicates the goal can converge with a direct response";
  graph.estimated_complexity = 1U;
  return graph;
}

std::vector<PlanNode> PlanGraphBuilder::expand_goal_into_nodes(
    const PlanningRequest& request) const {
  const auto plan_id = make_plan_id(request.request_id);
  const auto goal_summary = derive_goal_summary(request);
  const auto success_signal = derive_success_signal(request);
  const auto evidence_refs = collect_evidence_refs(request);
  const auto primary_action_kind = derive_primary_action_kind(request);

  std::vector<PlanNode> nodes;
  nodes.push_back(PlanNode{.node_id = make_node_id(plan_id, "stabilize-inputs"),
                           .objective = std::string("Stabilize planning inputs for ") +
                                        goal_summary,
                           .success_signal =
                               "planner has an actionable view of the current goal",
                           .action_kind_hint = "analysis",
                           .depends_on = {},
                           .evidence_refs = evidence_refs});

  const auto& hypotheses = optional_vector_or_empty(request.belief_state.hypotheses);
  if (!hypotheses.empty()) {
    nodes.push_back(PlanNode{.node_id = make_node_id(plan_id, "verify-hypothesis"),
                             .objective = std::string("Verify critical hypothesis: ") +
                                          hypotheses.front(),
                             .success_signal =
                                 "key belief dependency confirmed or rejected",
                             .action_kind_hint = "verification",
                             .depends_on = {},
                             .evidence_refs = evidence_refs});
  }

  const auto tool_hint = first_tool_hint(request.perception_result);
  nodes.push_back(PlanNode{.node_id = make_node_id(plan_id, "primary-step"),
                           .objective = tool_hint.empty()
                                            ? goal_summary
                                            : std::string("Use ") + tool_hint +
                                                  " to advance: " + goal_summary,
                           .success_signal = success_signal,
                           .action_kind_hint = primary_action_kind,
                           .depends_on = {},
                           .evidence_refs = evidence_refs});

  nodes.push_back(PlanNode{.node_id = make_node_id(plan_id, "validate-goal"),
                           .objective =
                               "Validate the outcome against the goal success criteria",
                           .success_signal = success_signal,
                           .action_kind_hint = "validation",
                           .depends_on = {},
                           .evidence_refs = evidence_refs});

  return nodes;
}

PlanGraph PlanGraphBuilder::compress_plan_when_budget_tight(
    PlanGraph graph,
    const PlanBuildLimits& limits) const {
  if (graph.nodes.empty()) {
    graph.estimated_complexity = static_cast<std::uint32_t>(graph.open_questions.size());
    return graph;
  }

  const auto target_node_count =
      std::max<std::uint32_t>(1U, std::min(limits.max_plan_nodes, limits.max_plan_depth));
  if (graph.nodes.size() <= target_node_count) {
    graph.estimated_complexity = static_cast<std::uint32_t>(graph.nodes.size() +
                                                            graph.open_questions.size());
    graph.edges = build_edges_from_nodes(graph.nodes);
    return graph;
  }

  std::vector<PlanNode> compressed_nodes;
  compressed_nodes.reserve(target_node_count);
  if (target_node_count == 1U) {
    compressed_nodes.push_back(merge_nodes(graph.nodes,
                                           make_node_id(graph.plan_id, "compressed-root"),
                                           graph.nodes.back().success_signal,
                                           graph.nodes.back().action_kind_hint));
  } else {
    compressed_nodes.push_back(graph.nodes.front());

    const std::vector<PlanNode> middle_nodes(graph.nodes.begin() + 1U,
                                             graph.nodes.end() - 1U);
    const auto middle_slots = static_cast<std::size_t>(target_node_count - 2U);
    if (middle_nodes.empty()) {
      compressed_nodes.push_back(graph.nodes.back());
    } else if (middle_slots == 0U) {
      compressed_nodes.push_back(merge_nodes(
          std::vector<PlanNode>(graph.nodes.begin() + 1U, graph.nodes.end()),
          make_node_id(graph.plan_id, "compressed-terminal"),
          graph.nodes.back().success_signal, graph.nodes.back().action_kind_hint));
    } else {
      const auto chunk_size =
          (middle_nodes.size() + middle_slots - 1U) / middle_slots;
      for (std::size_t index = 0U; index < middle_nodes.size();) {
        const auto end_index = std::min(middle_nodes.size(), index + chunk_size);
        compressed_nodes.push_back(merge_nodes(
            std::vector<PlanNode>(middle_nodes.begin() + index,
                                  middle_nodes.begin() + end_index),
            make_node_id(graph.plan_id,
                         std::string("compressed-middle-") +
                             std::to_string(compressed_nodes.size())),
            std::string{}, "compressed_step"));
        index = end_index;
      }
      compressed_nodes.push_back(graph.nodes.back());
    }
  }

  graph.nodes = std::move(compressed_nodes);
  graph.edges = build_edges_from_nodes(graph.nodes);
  graph.plan_rationale += " | compressed_for_budget";
  graph.estimated_complexity = static_cast<std::uint32_t>(graph.nodes.size() +
                                                          graph.open_questions.size());
  return graph;
}

bool PlanGraphBuilder::validate_plan_graph(const PlanGraph& graph,
                                           const PlanBuildLimits& limits) const {
  if (graph.plan_id.empty() || graph.plan_rationale.empty()) {
    return false;
  }

  if (graph.nodes.empty()) {
    return !graph.open_questions.empty() &&
           graph.estimated_complexity == graph.open_questions.size();
  }

  if (graph.nodes.size() > limits.max_plan_nodes) {
    return false;
  }

  if (!all_nodes_have_required_fields(graph)) {
    return false;
  }

  if (longest_path_depth(graph) > limits.max_plan_depth) {
    return false;
  }

  return graph.estimated_complexity > 0U;
}

PlanGraph PlanGraphBuilder::build_plan_graph(const PlanningRequest& request) const {
  const auto limits = derive_limits(request.budget_context);
  if (request.perception_result.requires_clarification ||
      !request.perception_result.clarification_questions.empty()) {
    auto graph = build_clarification_plan(request, limits);
    if (validate_plan_graph(graph, limits)) {
      return graph;
    }
    return build_direct_response_plan(request, limits);
  }

  if (request.perception_result.task_type == "direct_response") {
    auto graph = build_direct_response_plan(request, limits);
    graph.edges = build_edges_from_nodes(graph.nodes);
    return graph;
  }

  PlanGraph graph;
  graph.plan_id = make_plan_id(request.request_id);
  graph.revision = 1U;
  graph.nodes = expand_goal_into_nodes(request);
  graph.edges = build_edges_from_nodes(graph.nodes);
  graph.plan_rationale = limits.degraded_mode
                             ? "planner built a shallow plan under budget pressure"
                             : "planner expanded the goal into a staged execution graph";
  graph.estimated_complexity = static_cast<std::uint32_t>(graph.nodes.size());
  graph = compress_plan_when_budget_tight(std::move(graph), limits);

  if (!validate_plan_graph(graph, limits)) {
    auto fallback = build_direct_response_plan(request, limits);
    fallback.edges = build_edges_from_nodes(fallback.nodes);
    return fallback;
  }

  return graph;
}

ReplanResult PlanGraphBuilder::build_replan_graph(
    const ReplanRequest& request) const {
  const auto limits = derive_limits(request.budget_context);
  ReplanResult result;
  result.new_plan = request.active_plan;
  result.new_plan.plan_id = result.new_plan.plan_id.empty()
                                ? make_plan_id(request.request_id)
                                : result.new_plan.plan_id;
  result.new_plan.revision = request.active_plan.revision + 1U;
  result.replan_reason = derive_replan_reason(request);

  const auto observation_failed =
      request.latest_observation.success.has_value() &&
      !request.latest_observation.success.value();
  const auto evidence_refs = collect_replan_evidence_refs(request);

  if (!result.new_plan.nodes.empty()) {
    result.replaced_node_ids.push_back(result.new_plan.nodes.back().node_id);
    result.new_plan.nodes.pop_back();
  }

  result.new_plan.nodes.push_back(PlanNode{
      .node_id = make_node_id(
          result.new_plan.plan_id,
          std::string("replan-step-") + std::to_string(result.new_plan.revision)),
      .objective = derive_recovery_objective(request),
      .success_signal = observation_failed
                            ? std::string("blocking issue mitigated")
                            : std::string("latest observation integrated into the plan"),
      .action_kind_hint = observation_failed ? "recovery_analysis"
                                             : "observation_update",
      .depends_on = {},
      .evidence_refs = evidence_refs,
  });

  if (observation_failed && limits.max_plan_nodes > 1U && limits.max_plan_depth > 1U) {
    result.new_plan.nodes.push_back(PlanNode{
        .node_id = make_node_id(
            result.new_plan.plan_id,
            std::string("revalidate-") + std::to_string(result.new_plan.revision)),
        .objective = "Re-validate progress against the goal success criteria",
        .success_signal = optional_string_or(request.goal_contract.success_criteria,
                                             "goal progress has recovered"),
        .action_kind_hint = "validation",
        .depends_on = {},
        .evidence_refs = evidence_refs,
    });
  }

  result.new_plan.edges = build_edges_from_nodes(result.new_plan.nodes);
  result.new_plan.plan_rationale = request.active_plan.plan_rationale.empty()
                                       ? result.replan_reason
                                       : request.active_plan.plan_rationale + " | " +
                                             result.replan_reason;
  result.new_plan.estimated_complexity =
      static_cast<std::uint32_t>(result.new_plan.nodes.size() +
                                 result.new_plan.open_questions.size());
  result.new_plan = compress_plan_when_budget_tight(std::move(result.new_plan), limits);

  if (!validate_plan_graph(result.new_plan, limits)) {
    result.new_plan.nodes = {PlanNode{.node_id = make_node_id(result.new_plan.plan_id,
                                                              "fallback-recovery"),
                                      .objective = optional_string_or(
                                          request.goal_contract.goal_description,
                                          "re-stabilize the goal after failure"),
                                      .success_signal = optional_string_or(
                                          request.goal_contract.success_criteria,
                                          "goal can continue safely"),
                                      .action_kind_hint = "recovery_analysis",
                                      .depends_on = {},
                                      .evidence_refs = evidence_refs}};
    result.new_plan.edges.clear();
    result.new_plan.open_questions.clear();
    result.new_plan.plan_rationale = "fallback recovery plan used after graph validation failed";
    result.new_plan.estimated_complexity = 1U;
  }

  result.confidence = observation_failed ? kReplanConfidence : kSteadyPlanConfidence;
  return result;
}

}  // namespace dasall::cognition::planning
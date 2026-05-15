#include "projection/PlanGraphStructuredProjector.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dasall::cognition::projection {
namespace {

using dasall::cognition::validation::StructuredPayloadArrayView;
using dasall::cognition::validation::StructuredPayloadView;

[[nodiscard]] contracts::ErrorInfo make_projection_error(std::string field_path,
                                                         std::string message) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(contracts::ResultCode::ValidationFieldMissing),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
          .message = std::move(message),
          .stage = "planning",
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition.plan_graph_structured_projector",
          .ref_id = std::move(field_path),
      },
  };
}

[[nodiscard]] PlanGraphProjectionResult make_projection_failure(std::string field_path,
                                                                std::string message,
                                                                std::string diagnostic) {
  PlanGraphProjectionResult result;
  result.ok = false;
  result.error_info = make_projection_error(std::move(field_path), std::move(message));
  result.diagnostics.push_back(std::move(diagnostic));
  return result;
}

[[nodiscard]] std::optional<std::string> read_required_string(const StructuredPayloadView& object_view,
                                                              std::string_view field_path) {
  const auto value = object_view.read_string(field_path);
  if (!value.has_value() || value->empty()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<std::vector<std::string>> read_string_list(
    const StructuredPayloadView& object_view,
    std::string_view field_path) {
  if (!object_view.has_field(field_path)) {
    return std::vector<std::string>{};
  }

  const auto list_view = object_view.read_list(field_path);
  if (!list_view.has_value()) {
    return std::nullopt;
  }

  std::vector<std::string> values;
  values.reserve(list_view->size());
  for (std::size_t index = 0; index < list_view->size(); ++index) {
    const auto value = list_view->read_string(index);
    if (!value.has_value()) {
      return std::nullopt;
    }
    values.push_back(*value);
  }
  return values;
}

[[nodiscard]] std::optional<std::string> read_optional_string(const StructuredPayloadView& object_view,
                                                              std::string_view field_path) {
  if (!object_view.has_field(field_path)) {
    return std::string{};
  }

  const auto value = object_view.read_string(field_path);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<bool> read_optional_bool(const StructuredPayloadView& object_view,
                                                     std::string_view field_path,
                                                     bool default_value) {
  if (!object_view.has_field(field_path)) {
    return default_value;
  }

  const auto value = object_view.read_bool(field_path);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] std::optional<plan::PlanNode> project_plan_node(const StructuredPayloadView& node_view) {
  const auto node_id = read_required_string(node_view, "node_id");
  const auto objective = read_required_string(node_view, "objective");
  const auto success_signal = read_required_string(node_view, "success_signal");
  const auto action_kind_hint = read_required_string(node_view, "action_kind_hint");
  if (!node_id.has_value() || !objective.has_value() || !success_signal.has_value() ||
      !action_kind_hint.has_value()) {
    return std::nullopt;
  }

  plan::PlanNode node;
  node.node_id = *node_id;
  node.objective = *objective;
  node.success_signal = *success_signal;
  node.action_kind_hint = *action_kind_hint;
  const auto depends_on = read_string_list(node_view, "depends_on");
  if (!depends_on.has_value()) {
    return std::nullopt;
  }
  node.depends_on = *depends_on;

  const auto evidence_refs = read_string_list(node_view, "evidence_refs");
  if (!evidence_refs.has_value()) {
    return std::nullopt;
  }
  node.evidence_refs = *evidence_refs;
  return node;
}

[[nodiscard]] std::optional<plan::PlanEdge> project_plan_edge(const StructuredPayloadView& edge_view) {
  const auto from_node_id = read_required_string(edge_view, "from_node_id");
  const auto to_node_id = read_required_string(edge_view, "to_node_id");
  if (!from_node_id.has_value() || !to_node_id.has_value()) {
    return std::nullopt;
  }

  plan::PlanEdge edge;
  edge.from_node_id = *from_node_id;
  edge.to_node_id = *to_node_id;
  const auto condition = read_optional_string(edge_view, "condition");
  if (!condition.has_value()) {
    return std::nullopt;
  }
  edge.condition = *condition;

  const auto evidence_refs = read_string_list(edge_view, "evidence_refs");
  if (!evidence_refs.has_value()) {
    return std::nullopt;
  }
  edge.evidence_refs = *evidence_refs;
  return edge;
}

[[nodiscard]] std::optional<plan::PlanOpenQuestion> project_open_question(
    const StructuredPayloadView& question_view) {
  const auto question_id = read_required_string(question_view, "question_id");
  const auto question = read_required_string(question_view, "question");
  const auto reason = read_required_string(question_view, "reason");
  if (!question_id.has_value() || !question.has_value() || !reason.has_value()) {
    return std::nullopt;
  }

  plan::PlanOpenQuestion open_question;
  open_question.question_id = *question_id;
  open_question.question = *question;
  open_question.reason = *reason;
  const auto blocks_plan = read_optional_bool(question_view, "blocks_plan", true);
  if (!blocks_plan.has_value()) {
    return std::nullopt;
  }
  open_question.blocks_plan = *blocks_plan;

  const auto evidence_refs = read_string_list(question_view, "evidence_refs");
  if (!evidence_refs.has_value()) {
    return std::nullopt;
  }
  open_question.evidence_refs = *evidence_refs;
  return open_question;
}

template <typename Item, typename ProjectFn>
[[nodiscard]] std::optional<std::vector<Item>> project_object_array(
    const StructuredPayloadView& payload_view,
    std::string_view field_path,
    ProjectFn&& project_fn) {
  const auto list_view = payload_view.read_list(field_path);
  if (!list_view.has_value()) {
    return std::nullopt;
  }

  std::vector<Item> items;
  items.reserve(list_view->size());
  for (std::size_t index = 0; index < list_view->size(); ++index) {
    const auto object_view = list_view->read_object(index);
    if (!object_view.has_value()) {
      return std::nullopt;
    }

    const auto projected = project_fn(*object_view);
    if (!projected.has_value()) {
      return std::nullopt;
    }
    items.push_back(*projected);
  }
  return items;
}

}  // namespace

PlanGraphProjectionResult PlanGraphStructuredProjector::project_plan_graph(
    const StructuredPayloadView& payload_view) const {
  const auto plan_id = read_required_string(payload_view, "plan_id");
  if (!plan_id.has_value()) {
    return make_projection_failure("plan_id",
                                   "planning payload must contain a non-empty plan_id",
                                   "structured_output.projection_failed:planning:plan_id");
  }

  const auto revision = payload_view.read_number("revision");
  if (!revision.has_value() || *revision < 0.0) {
    return make_projection_failure("revision",
                                   "planning payload must contain a non-negative revision",
                                   "structured_output.projection_failed:planning:revision");
  }

  const auto estimated_complexity = payload_view.read_number("estimated_complexity");
  if (!estimated_complexity.has_value() || *estimated_complexity < 0.0) {
    return make_projection_failure(
        "estimated_complexity",
        "planning payload must contain a non-negative estimated_complexity",
        "structured_output.projection_failed:planning:estimated_complexity");
  }

  const auto nodes = project_object_array<plan::PlanNode>(
      payload_view,
      "nodes",
      [](const StructuredPayloadView& node_view) { return project_plan_node(node_view); });
  if (!nodes.has_value()) {
    return make_projection_failure("nodes",
                                   "planning payload nodes must be valid structured plan nodes",
                                   "structured_output.projection_failed:planning:nodes");
  }

  const auto edges = project_object_array<plan::PlanEdge>(
      payload_view,
      "edges",
      [](const StructuredPayloadView& edge_view) { return project_plan_edge(edge_view); });
  if (!edges.has_value()) {
    return make_projection_failure("edges",
                                   "planning payload edges must be valid structured plan edges",
                                   "structured_output.projection_failed:planning:edges");
  }

  std::vector<plan::PlanOpenQuestion> open_questions;
  if (payload_view.has_field("open_questions")) {
    const auto projected_open_questions = project_object_array<plan::PlanOpenQuestion>(
        payload_view,
        "open_questions",
        [](const StructuredPayloadView& question_view) {
          return project_open_question(question_view);
        });
    if (!projected_open_questions.has_value()) {
      return make_projection_failure(
          "open_questions",
          "planning payload open_questions must be valid structured open questions",
          "structured_output.projection_failed:planning:open_questions");
    }
    open_questions = *projected_open_questions;
  }

  plan::PlanGraph plan_graph;
  plan_graph.plan_id = *plan_id;
  plan_graph.revision = static_cast<std::uint32_t>(*revision);
  plan_graph.nodes = *nodes;
  plan_graph.edges = *edges;
  plan_graph.open_questions = std::move(open_questions);
  const auto plan_rationale = read_optional_string(payload_view, "plan_rationale");
  if (!plan_rationale.has_value()) {
    return make_projection_failure(
        "plan_rationale",
        "planning payload plan_rationale must be a string when present",
        "structured_output.projection_failed:planning:plan_rationale");
  }
  plan_graph.plan_rationale = *plan_rationale;
  plan_graph.estimated_complexity = static_cast<std::uint32_t>(*estimated_complexity);

  PlanGraphProjectionResult result;
  result.ok = true;
  result.plan_graph = std::move(plan_graph);
  result.diagnostics.push_back("structured_output.projected:planning");
  return result;
}

}  // namespace dasall::cognition::projection
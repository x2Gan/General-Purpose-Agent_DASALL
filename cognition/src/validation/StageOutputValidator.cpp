#include "validation/StageOutputValidator.h"

#include "validation/StructuredPayloadView.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dasall::cognition::validation {
namespace {

[[nodiscard]] std::string top_level_field_name(std::string_view field_path) {
  const auto separator = field_path.find('.');
  return std::string(separator == std::string_view::npos
                         ? field_path
                         : field_path.substr(0U, separator));
}

[[nodiscard]] bool has_allowed_extension_prefix(const StageSchemaSpec& schema_spec,
                                                std::string_view field_name) {
  if (schema_spec.unknown_field_policy != UnknownFieldPolicy::AllowRegisteredExtensions) {
    return false;
  }

  return std::any_of(schema_spec.allowed_extension_prefixes.begin(),
                     schema_spec.allowed_extension_prefixes.end(),
                     [&](const auto& prefix) {
                       return !prefix.empty() && field_name.starts_with(prefix);
                     });
}

[[nodiscard]] bool is_known_top_level_field(const StageSchemaSpec& schema_spec,
                                            std::string_view field_name) {
  if (std::find(schema_spec.known_top_level_fields.begin(),
                schema_spec.known_top_level_fields.end(),
                field_name) != schema_spec.known_top_level_fields.end()) {
    return true;
  }

  const auto known_in_paths = [&](const auto& field_paths) {
    return std::any_of(field_paths.begin(), field_paths.end(), [&](const auto& path_like) {
      return top_level_field_name(path_like.field_path) == field_name;
    });
  };

  if (std::any_of(schema_spec.required_fields.begin(),
                  schema_spec.required_fields.end(),
                  [&](const auto& required_field) {
                    return top_level_field_name(required_field) == field_name;
                  })) {
    return true;
  }

  return known_in_paths(schema_spec.enum_constraints) ||
         known_in_paths(schema_spec.numeric_bounds) ||
         known_in_paths(schema_spec.list_constraints);
}

[[nodiscard]] contracts::ErrorInfo make_validation_error_info(const std::string& stage_name,
                                                              const ValidationIssue& issue) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(contracts::ResultCode::ValidationFieldMissing),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(contracts::ResultCode::ValidationFieldMissing),
          .message = issue.message,
          .stage = stage_name,
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition.stage_output_validator",
          .ref_id = issue.field_path,
      },
  };
}

[[nodiscard]] ValidationResult finalize_result(ValidationIssueSet issue_set,
                                               std::string stage_name,
                                               std::vector<std::string> diagnostics) {
  ValidationResult result;
  result.ok = issue_set.empty();
  result.issue_set = std::move(issue_set);
  result.diagnostics = std::move(diagnostics);
  if (!result.ok && !result.issue_set.issues.empty()) {
    result.error_info = make_validation_error_info(stage_name, result.issue_set.issues.front());
    result.diagnostics.push_back(std::string("validation_failed:") + stage_name);
  }
  return result;
}

[[nodiscard]] std::uint32_t compute_plan_depth(
    const std::unordered_map<std::string, std::vector<std::string>>& outgoing_edges,
    const std::string& node_id,
    std::unordered_map<std::string, std::uint32_t>& memo) {
  const auto memo_it = memo.find(node_id);
  if (memo_it != memo.end()) {
    return memo_it->second;
  }

  auto depth = 1U;
  const auto outgoing_it = outgoing_edges.find(node_id);
  if (outgoing_it != outgoing_edges.end()) {
    for (const auto& next_node_id : outgoing_it->second) {
      depth = std::max(depth, 1U + compute_plan_depth(outgoing_edges, next_node_id, memo));
    }
  }

  memo.emplace(node_id, depth);
  return depth;
}

}  // namespace

void ValidationIssueSet::add(ValidationIssueCode code,
                             std::string field_path,
                             std::string message) {
  issues.push_back(ValidationIssue{
      .code = code,
      .field_path = std::move(field_path),
      .message = std::move(message),
  });
}

ValidationResult StageOutputValidator::validate_stage_output(
    const llm_bridge::StageLlmCallResult& stage_result,
    const StageSchemaSpec& schema_spec) const {
  ValidationIssueSet issue_set;
  std::vector<std::string> diagnostics;

  if (!stage_result.response.has_value() || !stage_result.response->content_payload.has_value()) {
    issue_set.add(ValidationIssueCode::MissingRequiredField,
                  "response.content_payload",
                  "stage output must contain a normalized content payload");
    return finalize_result(std::move(issue_set), schema_spec.stage_name, std::move(diagnostics));
  }

  const auto& payload = *stage_result.response->content_payload;
  const auto payload_view = StructuredPayloadView::parse_structured_payload(payload);
  if (!payload_view.has_value()) {
    issue_set.add(ValidationIssueCode::MalformedJson,
                  "response.content_payload",
                  "stage output must be a well-formed JSON object");
    return finalize_result(std::move(issue_set), schema_spec.stage_name, std::move(diagnostics));
  }

  auto field_names = payload_view->field_names();
  std::sort(field_names.begin(), field_names.end());
  for (const auto& field_name : field_names) {
    if (!is_known_top_level_field(schema_spec, field_name) &&
        !has_allowed_extension_prefix(schema_spec, field_name)) {
      issue_set.add(ValidationIssueCode::UnknownField,
                    field_name,
                    std::string("field is not allowed by schema: ") + field_name);
    }
  }

  for (const auto& field_path : schema_spec.required_fields) {
    if (!payload_view->has_field(field_path)) {
      issue_set.add(ValidationIssueCode::MissingRequiredField,
                    field_path,
                    std::string("missing required field: ") + field_path);
    }
  }

  for (const auto& enum_constraint : schema_spec.enum_constraints) {
    const auto value = payload_view->read_string(enum_constraint.field_path);
    if (!value.has_value() ||
        std::find(enum_constraint.allowed_values.begin(),
                  enum_constraint.allowed_values.end(),
                  *value) == enum_constraint.allowed_values.end()) {
      issue_set.add(ValidationIssueCode::InvalidEnumLiteral,
                    enum_constraint.field_path,
                    std::string("enum literal is not allowed for field: ") +
                        enum_constraint.field_path);
    }
  }

  for (const auto& numeric_constraint : schema_spec.numeric_bounds) {
    const auto value = payload_view->read_number(numeric_constraint.field_path);
    if (!value.has_value()) {
      issue_set.add(ValidationIssueCode::NumericOutOfRange,
                    numeric_constraint.field_path,
                    std::string("numeric field missing or invalid: ") +
                        numeric_constraint.field_path);
      continue;
    }
    if ((numeric_constraint.min_value.has_value() && *value < *numeric_constraint.min_value) ||
        (numeric_constraint.max_value.has_value() && *value > *numeric_constraint.max_value)) {
      issue_set.add(ValidationIssueCode::NumericOutOfRange,
                    numeric_constraint.field_path,
                    std::string("numeric field is out of range: ") +
                        numeric_constraint.field_path);
    }
  }

  for (const auto& list_constraint : schema_spec.list_constraints) {
    const auto list_view = payload_view->read_list(list_constraint.field_path);
    if (!list_view.has_value()) {
      issue_set.add(ValidationIssueCode::ListSizeOutOfRange,
                    list_constraint.field_path,
                    std::string("list field missing or invalid: ") +
                        list_constraint.field_path);
      continue;
    }
    if (list_view->size() < list_constraint.min_items ||
        (list_constraint.max_items.has_value() &&
         list_view->size() > *list_constraint.max_items)) {
      issue_set.add(ValidationIssueCode::ListSizeOutOfRange,
                    list_constraint.field_path,
                    std::string("list field violates size constraints: ") +
                        list_constraint.field_path);
    }
  }

  return finalize_result(std::move(issue_set), schema_spec.stage_name, std::move(diagnostics));
}

ValidationResult StageOutputValidator::validate_plan_graph_invariants(
    const plan::PlanGraph& plan_graph,
    const std::uint32_t max_plan_nodes,
    const std::uint32_t max_plan_depth) const {
  ValidationIssueSet issue_set;
  std::vector<std::string> diagnostics;

  if (plan_graph.plan_id.empty()) {
    issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                  "plan_id",
                  "plan graph must carry a non-empty plan_id");
  }

  if (plan_graph.nodes.empty()) {
    issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                  "nodes",
                  "plan graph must include at least one node");
  }

  if (max_plan_nodes > 0U && plan_graph.nodes.size() > max_plan_nodes) {
    issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                  "nodes",
                  "plan graph exceeds the configured node cap");
  }

  std::unordered_set<std::string> known_node_ids;
  std::unordered_map<std::string, std::vector<std::string>> outgoing_edges;
  std::unordered_map<std::string, std::uint32_t> indegree;
  for (const auto& node : plan_graph.nodes) {
    if (node.node_id.empty()) {
      issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                    "nodes.node_id",
                    "plan nodes must carry a non-empty node_id");
      continue;
    }
    if (!known_node_ids.insert(node.node_id).second) {
      issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                    "nodes.node_id",
                    "plan graph must not contain duplicate node ids");
    }
    indegree.emplace(node.node_id, 0U);
  }

  for (const auto& edge : plan_graph.edges) {
    if (!known_node_ids.contains(edge.from_node_id) || !known_node_ids.contains(edge.to_node_id)) {
      issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                    "edges",
                    "plan graph edges must reference known node ids");
      continue;
    }

    outgoing_edges[edge.from_node_id].push_back(edge.to_node_id);
    ++indegree[edge.to_node_id];
  }

  std::queue<std::string> zero_indegree_nodes;
  for (const auto& [node_id, degree] : indegree) {
    if (degree == 0U) {
      zero_indegree_nodes.push(node_id);
    }
  }

  std::size_t visited_node_count = 0U;
  auto indegree_copy = indegree;
  while (!zero_indegree_nodes.empty()) {
    const auto node_id = zero_indegree_nodes.front();
    zero_indegree_nodes.pop();
    ++visited_node_count;

    for (const auto& next_node_id : outgoing_edges[node_id]) {
      auto& next_degree = indegree_copy[next_node_id];
      --next_degree;
      if (next_degree == 0U) {
        zero_indegree_nodes.push(next_node_id);
      }
    }
  }

  if (!plan_graph.nodes.empty() && visited_node_count != known_node_ids.size()) {
    issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                  "edges",
                  "plan graph must remain acyclic");
  }

  if (max_plan_depth > 0U && !plan_graph.nodes.empty() && issue_set.empty()) {
    std::unordered_map<std::string, std::uint32_t> memo;
    auto computed_depth = 0U;
    for (const auto& node : plan_graph.nodes) {
      computed_depth = std::max(computed_depth, compute_plan_depth(outgoing_edges, node.node_id, memo));
    }
    if (computed_depth > max_plan_depth) {
      issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                    "nodes",
                    "plan graph exceeds the configured depth cap");
    }
  }

  return finalize_result(std::move(issue_set), "planning", std::move(diagnostics));
}

ValidationResult StageOutputValidator::validate_action_decision_invariants(
    const decision::ActionDecision& action_decision) const {
  ValidationIssueSet issue_set;
  std::vector<std::string> diagnostics;

  const auto has_selected_node = action_decision.selected_node_id.has_value() &&
                                 !action_decision.selected_node_id->empty();
  const auto has_clarification_question = action_decision.clarification_question.has_value() &&
                                          !action_decision.clarification_question->empty();
  const auto has_tool_intent = action_decision.tool_intent_hint.has_value() &&
                               !action_decision.tool_intent_hint->tool_name.empty();
  const auto has_response_outline = action_decision.response_outline.has_value() &&
                                    !action_decision.response_outline->summary.empty();

  switch (action_decision.decision_kind) {
    case decision::ActionDecisionKind::ExecuteAction:
      if (!has_selected_node) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "selected_node_id",
                      "execute_action decisions require a selected plan node");
      }
      if (!has_tool_intent) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "tool_intent_hint.tool_name",
                      "execute_action decisions require a tool intent hint");
      }
      if (action_decision.clarification_needed || has_clarification_question) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "clarification_question",
                      "execute_action decisions must not simultaneously request clarification");
      }
      break;
    case decision::ActionDecisionKind::DirectResponse:
    case decision::ActionDecisionKind::ConvergeSafe:
      if (!has_response_outline) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "response_outline.summary",
                      "response decisions require a non-empty response outline summary");
      }
      if (has_tool_intent) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "tool_intent_hint.tool_name",
                      "response decisions must not carry executable tool intent");
      }
      if (action_decision.clarification_needed || has_clarification_question) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "clarification_question",
                      "response decisions must not simultaneously request clarification");
      }
      break;
    case decision::ActionDecisionKind::AskClarification:
      if (!action_decision.clarification_needed ||
          !has_clarification_question) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "clarification_question",
                      "clarification decisions require a clarification question");
      }
      if (has_tool_intent) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "tool_intent_hint.tool_name",
                      "clarification decisions must not carry a tool intent hint");
      }
      break;
    case decision::ActionDecisionKind::NoDecision:
      break;
  }

  return finalize_result(std::move(issue_set), "execution", std::move(diagnostics));
}

ValidationResult StageOutputValidator::validate_response_envelope(
    const ResponseBuildResult& response_result) const {
  ValidationIssueSet issue_set;
  std::vector<std::string> diagnostics;

  if (response_result.agent_result.has_value()) {
    const auto& agent_result = *response_result.agent_result;
    if (!agent_result.status.has_value() ||
        *agent_result.status == contracts::AgentResultStatus::Unspecified) {
      issue_set.add(ValidationIssueCode::ResponseEnvelopeInvariant,
                    "agent_result.status",
                    "response envelope requires a concrete AgentResult status");
    }

    if (!agent_result.response_text.has_value()) {
      issue_set.add(ValidationIssueCode::ResponseEnvelopeInvariant,
                    "agent_result.response_text",
                    "response envelope requires a response_text field");
    }

    if (agent_result.status.has_value() &&
        (*agent_result.status == contracts::AgentResultStatus::Failed ||
         *agent_result.status == contracts::AgentResultStatus::Timeout) &&
        !response_result.error_info.has_value()) {
      issue_set.add(ValidationIssueCode::ResponseEnvelopeInvariant,
                    "error_info",
                    "failed response envelopes must carry error_info");
    }

    if (response_result.fallback_used && agent_result.status.has_value() &&
        *agent_result.status == contracts::AgentResultStatus::Completed) {
      issue_set.add(ValidationIssueCode::ResponseEnvelopeInvariant,
                    "fallback_used",
                    "fallback responses must not report a fully completed status");
    }
  } else if (!response_result.error_info.has_value()) {
    issue_set.add(ValidationIssueCode::ResponseEnvelopeInvariant,
                  "agent_result",
                  "response envelope must carry either an AgentResult or ErrorInfo");
  }

  return finalize_result(std::move(issue_set), "response", std::move(diagnostics));
}

}  // namespace dasall::cognition::validation
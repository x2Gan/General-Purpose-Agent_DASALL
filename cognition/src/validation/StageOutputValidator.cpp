#include "validation/StageOutputValidator.h"

#include "checkpoint/ReflectionDecisionGuards.h"
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

[[nodiscard]] std::optional<StructuredPayloadArrayView> parse_array_token(
    const StructuredPayloadToken& token) {
  if (token.kind != JsonTokenKind::Array) {
    return std::nullopt;
  }

  const auto items = detail::parse_json_array_items(token.raw);
  if (!items.has_value()) {
    return std::nullopt;
  }
  return StructuredPayloadArrayView(*items);
}

[[nodiscard]] std::optional<std::vector<StructuredPayloadToken>> collect_matching_tokens(
    const StructuredPayloadToken& token,
    std::string_view remaining_path);

[[nodiscard]] std::optional<std::vector<StructuredPayloadToken>> collect_matching_tokens(
    const StructuredPayloadView& payload_view,
    std::string_view field_path) {
  if (field_path.empty()) {
    return std::nullopt;
  }

  const auto separator = field_path.find('.');
  const auto segment = separator == std::string_view::npos ? field_path
                                                           : field_path.substr(0U, separator);
  const auto token = payload_view.field_token(segment);
  if (!token.has_value()) {
    return std::nullopt;
  }

  if (separator == std::string_view::npos) {
    return std::vector<StructuredPayloadToken>{*token};
  }

  return collect_matching_tokens(*token, field_path.substr(separator + 1U));
}

[[nodiscard]] std::optional<std::vector<StructuredPayloadToken>> collect_matching_tokens(
    const StructuredPayloadToken& token,
    std::string_view remaining_path) {
  if (remaining_path.empty()) {
    return std::vector<StructuredPayloadToken>{token};
  }

  if (token.kind == JsonTokenKind::Object) {
    const auto nested_view = StructuredPayloadView::parse_structured_payload(token.raw);
    if (!nested_view.has_value()) {
      return std::nullopt;
    }
    return collect_matching_tokens(*nested_view, remaining_path);
  }

  if (token.kind != JsonTokenKind::Array) {
    return std::nullopt;
  }

  const auto array_view = parse_array_token(token);
  if (!array_view.has_value()) {
    return std::nullopt;
  }

  std::vector<StructuredPayloadToken> tokens;
  for (std::size_t index = 0; index < array_view->size(); ++index) {
    const auto* item_token = array_view->token_at(index);
    if (item_token == nullptr) {
      return std::nullopt;
    }

    const auto nested_tokens = collect_matching_tokens(*item_token, remaining_path);
    if (!nested_tokens.has_value()) {
      return std::nullopt;
    }
    tokens.insert(tokens.end(), nested_tokens->begin(), nested_tokens->end());
  }

  return tokens;
}

[[nodiscard]] bool all_string_tokens_match(const std::vector<StructuredPayloadToken>& tokens,
                                           const std::vector<std::string>& allowed_values) {
  if (tokens.empty()) {
    return false;
  }

  return std::all_of(tokens.begin(), tokens.end(), [&](const auto& token) {
    if (token.kind != JsonTokenKind::String) {
      return false;
    }

    const auto value = detail::parse_json_string(token.raw);
    return value.has_value() &&
           std::find(allowed_values.begin(), allowed_values.end(), *value) !=
               allowed_values.end();
  });
}

[[nodiscard]] bool all_numeric_tokens_within_bounds(
    const std::vector<StructuredPayloadToken>& tokens,
    const NumericConstraint& numeric_constraint) {
  if (tokens.empty()) {
    return false;
  }

  return std::all_of(tokens.begin(), tokens.end(), [&](const auto& token) {
    if (token.kind != JsonTokenKind::Number) {
      return false;
    }

    const auto value = detail::parse_json_number(token.raw);
    if (!value.has_value()) {
      return false;
    }

    return (!numeric_constraint.min_value.has_value() || *value >= *numeric_constraint.min_value) &&
           (!numeric_constraint.max_value.has_value() || *value <= *numeric_constraint.max_value);
  });
}

[[nodiscard]] bool all_list_tokens_within_bounds(const std::vector<StructuredPayloadToken>& tokens,
                                                 const ListConstraint& list_constraint) {
  if (tokens.empty()) {
    return false;
  }

  return std::all_of(tokens.begin(), tokens.end(), [&](const auto& token) {
    const auto array_view = parse_array_token(token);
    if (!array_view.has_value()) {
      return false;
    }

    return array_view->size() >= list_constraint.min_items &&
           (!list_constraint.max_items.has_value() ||
            array_view->size() <= *list_constraint.max_items);
  });
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
    const auto tokens = collect_matching_tokens(*payload_view, field_path);
    if (!tokens.has_value() || tokens->empty()) {
      issue_set.add(ValidationIssueCode::MissingRequiredField,
                    field_path,
                    std::string("missing required field: ") + field_path);
    }
  }

  for (const auto& enum_constraint : schema_spec.enum_constraints) {
    const auto tokens = collect_matching_tokens(*payload_view, enum_constraint.field_path);
    if (!tokens.has_value() ||
        !all_string_tokens_match(*tokens, enum_constraint.allowed_values)) {
      issue_set.add(ValidationIssueCode::InvalidEnumLiteral,
                    enum_constraint.field_path,
                    std::string("enum literal is not allowed for field: ") +
                        enum_constraint.field_path);
    }
  }

  for (const auto& numeric_constraint : schema_spec.numeric_bounds) {
    const auto tokens = collect_matching_tokens(*payload_view, numeric_constraint.field_path);
    if (!tokens.has_value() ||
        !all_numeric_tokens_within_bounds(*tokens, numeric_constraint)) {
      issue_set.add(ValidationIssueCode::NumericOutOfRange,
                    numeric_constraint.field_path,
                    std::string("numeric field missing or invalid: ") +
                        numeric_constraint.field_path);
    }
  }

  for (const auto& list_constraint : schema_spec.list_constraints) {
    const auto tokens = collect_matching_tokens(*payload_view, list_constraint.field_path);
    if (!tokens.has_value() || !all_list_tokens_within_bounds(*tokens, list_constraint)) {
      issue_set.add(ValidationIssueCode::ListSizeOutOfRange,
                    list_constraint.field_path,
                    std::string("list field missing or invalid: ") +
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
  const auto register_dependency_edge = [&outgoing_edges, &indegree](const std::string& from_node_id,
                                                                     const std::string& to_node_id) {
    auto& next_node_ids = outgoing_edges[from_node_id];
    if (std::find(next_node_ids.begin(), next_node_ids.end(), to_node_id) != next_node_ids.end()) {
      return;
    }

    next_node_ids.push_back(to_node_id);
    ++indegree[to_node_id];
  };
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

    register_dependency_edge(edge.from_node_id, edge.to_node_id);
  }

  for (const auto& node : plan_graph.nodes) {
    if (node.node_id.empty()) {
      continue;
    }

    std::unordered_set<std::string> seen_dependencies;
    for (const auto& dependency_node_id : node.depends_on) {
      if (dependency_node_id.empty()) {
        issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                      "nodes.depends_on",
                      "plan node dependencies must reference non-empty node ids");
        continue;
      }

      if (!seen_dependencies.insert(dependency_node_id).second) {
        issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                      "nodes.depends_on",
                      "plan node dependencies must not contain duplicates");
        continue;
      }

      if (dependency_node_id == node.node_id) {
        issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                      "nodes.depends_on",
                      "plan nodes must not depend on themselves");
        continue;
      }

      if (!known_node_ids.contains(dependency_node_id)) {
        issue_set.add(ValidationIssueCode::PlanGraphInvariant,
                      "nodes.depends_on",
                      "plan node dependencies must reference known node ids");
        continue;
      }

      register_dependency_edge(dependency_node_id, node.node_id);
    }
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
  const decision::ActionDecision& action_decision,
  const plan::PlanGraph* active_plan) const {
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
  auto selected_node_belongs_to_active_plan = false;
  if (has_selected_node && active_plan != nullptr) {
    selected_node_belongs_to_active_plan = std::any_of(
        active_plan->nodes.begin(),
        active_plan->nodes.end(),
        [&action_decision](const plan::PlanNode& node) {
          return node.node_id == *action_decision.selected_node_id;
        });
  }

  switch (action_decision.decision_kind) {
    case decision::ActionDecisionKind::ExecuteAction:
      if (!has_selected_node) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "selected_node_id",
                      "execute_action decisions require a selected plan node");
      } else if (active_plan != nullptr && !selected_node_belongs_to_active_plan) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "selected_node_id",
                      "execute_action decisions must reference a node from the active plan");
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
      if (has_selected_node) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "selected_node_id",
                      "response decisions must not carry a selected plan node");
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
      if (has_selected_node) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "selected_node_id",
                      "clarification decisions must not carry a selected plan node");
      }
      if (has_response_outline) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "response_outline.summary",
                      "clarification decisions must not carry a terminal response outline");
      }
      break;
    case decision::ActionDecisionKind::NoDecision:
      issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                    "decision_kind",
                    "authoritative action decisions must not remain undecided");
      break;
  }

  return finalize_result(std::move(issue_set), "execution", std::move(diagnostics));
}

[[nodiscard]] std::string reflection_issue_field_path(std::string_view reason) {
  if (reason.find("request_id") != std::string_view::npos) {
    return "request_id";
  }
  if (reason.find("decision_kind") != std::string_view::npos) {
    return "decision_kind";
  }
  if (reason.find("rationale") != std::string_view::npos) {
    return "rationale";
  }
  if (reason.find("goal_id") != std::string_view::npos) {
    return "goal_id";
  }
  if (reason.find("confidence") != std::string_view::npos) {
    return "confidence";
  }
  if (reason.find("hint_ref") != std::string_view::npos) {
    return "hint_ref";
  }
  if (reason.find("created_at") != std::string_view::npos) {
    return "created_at";
  }
  if (reason.find("relevant_observation_refs") != std::string_view::npos) {
    return "relevant_observation_refs";
  }
  if (reason.find("tags") != std::string_view::npos) {
    return "tags";
  }
  return "reflection_decision";
}

ValidationResult StageOutputValidator::validate_reflection_decision_invariants(
    const contracts::ReflectionDecision& reflection_decision) const {
  ValidationIssueSet issue_set;
  std::vector<std::string> diagnostics;

  const auto guard = contracts::validate_reflection_decision_field_rules(reflection_decision);
  if (!guard.ok) {
    issue_set.add(ValidationIssueCode::ReflectionDecisionInvariant,
                  reflection_issue_field_path(guard.reason),
                  std::string(guard.reason));
  }

  return finalize_result(std::move(issue_set), "reflection", std::move(diagnostics));
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
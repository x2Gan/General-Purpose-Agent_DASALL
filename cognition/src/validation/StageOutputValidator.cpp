#include "validation/StageOutputValidator.h"

#include <algorithm>
#include <cctype>
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

enum class JsonTokenKind : std::uint8_t {
  String = 0,
  Number,
  Object,
  Array,
  Boolean,
  Null,
  Unknown,
};

struct JsonToken {
  std::string raw;
  JsonTokenKind kind = JsonTokenKind::Unknown;
};

using JsonObjectFields = std::unordered_map<std::string, JsonToken>;

[[nodiscard]] std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }

  return value;
}

[[nodiscard]] std::string_view trim_view(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1U);
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1U);
  }

  return value;
}

void skip_ascii_ws(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
}

[[nodiscard]] bool consume_json_string(std::string_view text, std::size_t& cursor) {
  if (cursor >= text.size() || text[cursor] != '"') {
    return false;
  }

  ++cursor;
  bool escaped = false;
  while (cursor < text.size()) {
    const char current = text[cursor++];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (current == '\\') {
      escaped = true;
      continue;
    }
    if (current == '"') {
      return true;
    }
  }

  return false;
}

[[nodiscard]] std::optional<std::string> parse_json_string(std::string_view raw_token) {
  const auto trimmed = trim_view(raw_token);
  if (trimmed.size() < 2U || trimmed.front() != '"' || trimmed.back() != '"') {
    return std::nullopt;
  }

  std::string value;
  bool escaped = false;
  for (std::size_t index = 1U; index + 1U < trimmed.size(); ++index) {
    const char current = trimmed[index];
    if (escaped) {
      switch (current) {
        case '"':
        case '\\':
        case '/':
          value.push_back(current);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          value.push_back(current);
          break;
      }
      escaped = false;
      continue;
    }

    if (current == '\\') {
      escaped = true;
      continue;
    }

    value.push_back(current);
  }

  if (escaped) {
    return std::nullopt;
  }

  return value;
}

[[nodiscard]] JsonTokenKind classify_json_token(std::string_view raw_token) {
  const auto trimmed = trim_view(raw_token);
  if (trimmed.empty()) {
    return JsonTokenKind::Unknown;
  }

  switch (trimmed.front()) {
    case '"':
      return JsonTokenKind::String;
    case '{':
      return JsonTokenKind::Object;
    case '[':
      return JsonTokenKind::Array;
    case 't':
    case 'f':
      return JsonTokenKind::Boolean;
    case 'n':
      return JsonTokenKind::Null;
    default:
      if (trimmed.front() == '-' ||
          std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0) {
        return JsonTokenKind::Number;
      }
      return JsonTokenKind::Unknown;
  }
}

[[nodiscard]] std::optional<JsonToken> parse_json_value_token(std::string_view text,
                                                              std::size_t& cursor) {
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size()) {
    return std::nullopt;
  }

  const auto token_begin = cursor;
  if (text[cursor] == '"') {
    if (!consume_json_string(text, cursor)) {
      return std::nullopt;
    }
    return JsonToken{
        .raw = std::string(text.substr(token_begin, cursor - token_begin)),
        .kind = JsonTokenKind::String,
    };
  }

  int nested_objects = 0;
  int nested_arrays = 0;
  bool in_string = false;
  bool escaped = false;
  while (cursor < text.size()) {
    const char current = text[cursor];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        in_string = false;
      }
      ++cursor;
      continue;
    }

    switch (current) {
      case '"':
        in_string = true;
        ++cursor;
        continue;
      case '{':
        ++nested_objects;
        ++cursor;
        continue;
      case '}':
        if (nested_objects == 0 && nested_arrays == 0) {
          goto value_done;
        }
        --nested_objects;
        ++cursor;
        continue;
      case '[':
        ++nested_arrays;
        ++cursor;
        continue;
      case ']':
        if (nested_arrays == 0) {
          goto value_done;
        }
        --nested_arrays;
        ++cursor;
        continue;
      case ',':
        if (nested_objects == 0 && nested_arrays == 0) {
          goto value_done;
        }
        ++cursor;
        continue;
      default:
        ++cursor;
        continue;
    }
  }

value_done:
  if (in_string || nested_objects != 0 || nested_arrays != 0) {
    return std::nullopt;
  }

  const auto raw_token = trim_copy(std::string(text.substr(token_begin, cursor - token_begin)));
  if (raw_token.empty()) {
    return std::nullopt;
  }

  return JsonToken{
      .raw = raw_token,
      .kind = classify_json_token(raw_token),
  };
}

[[nodiscard]] std::optional<JsonObjectFields> parse_json_object_fields(std::string_view text) {
  std::size_t cursor = 0U;
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size() || text[cursor] != '{') {
    return std::nullopt;
  }
  ++cursor;

  JsonObjectFields fields;
  while (cursor < text.size()) {
    skip_ascii_ws(text, cursor);
    if (cursor >= text.size()) {
      return std::nullopt;
    }
    if (text[cursor] == '}') {
      ++cursor;
      skip_ascii_ws(text, cursor);
      return cursor == text.size() ? std::optional<JsonObjectFields>(std::move(fields))
                                   : std::nullopt;
    }

    const auto key_begin = cursor;
    if (!consume_json_string(text, cursor)) {
      return std::nullopt;
    }
    const auto key = parse_json_string(text.substr(key_begin, cursor - key_begin));
    if (!key.has_value()) {
      return std::nullopt;
    }

    skip_ascii_ws(text, cursor);
    if (cursor >= text.size() || text[cursor] != ':') {
      return std::nullopt;
    }
    ++cursor;

    const auto value = parse_json_value_token(text, cursor);
    if (!value.has_value()) {
      return std::nullopt;
    }
    fields[*key] = *value;

    skip_ascii_ws(text, cursor);
    if (cursor >= text.size()) {
      return std::nullopt;
    }
    if (text[cursor] == ',') {
      ++cursor;
      continue;
    }
    if (text[cursor] == '}') {
      ++cursor;
      skip_ascii_ws(text, cursor);
      return cursor == text.size() ? std::optional<JsonObjectFields>(std::move(fields))
                                   : std::nullopt;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<JsonToken> find_field_token_in_object(
    std::string_view payload,
    const std::string_view& field_path) {
  if (field_path.empty()) {
    return std::nullopt;
  }

  std::string current_object_text(payload);
  std::string remaining_path(field_path);
  while (true) {
    const auto object_fields = parse_json_object_fields(current_object_text);
    if (!object_fields.has_value()) {
      return std::nullopt;
    }

    const auto separator = remaining_path.find('.');
    const auto segment = separator == std::string::npos
                             ? remaining_path
                             : remaining_path.substr(0U, separator);
    const auto field_it = object_fields->find(segment);
    if (field_it == object_fields->end()) {
      return std::nullopt;
    }

    if (separator == std::string::npos) {
      return field_it->second;
    }
    if (field_it->second.kind != JsonTokenKind::Object) {
      return std::nullopt;
    }

    current_object_text = field_it->second.raw;
    remaining_path.erase(0U, separator + 1U);
  }
}

[[nodiscard]] std::optional<std::string> extract_string_field(const std::string& payload,
                                                              const std::string_view& field_path) {
  const auto token = find_field_token_in_object(payload, field_path);
  if (!token.has_value() || token->kind != JsonTokenKind::String) {
    return std::nullopt;
  }

  return parse_json_string(token->raw);
}

[[nodiscard]] std::optional<double> extract_number_field(const std::string& payload,
                                                         const std::string_view& field_path) {
  const auto token = find_field_token_in_object(payload, field_path);
  if (!token.has_value() || token->kind != JsonTokenKind::Number) {
    return std::nullopt;
  }

  const auto raw_value = trim_copy(token->raw);
  if (raw_value.empty()) {
    return std::nullopt;
  }

  try {
    std::size_t parsed_length = 0U;
    const auto number = std::stod(raw_value, &parsed_length);
    if (parsed_length != raw_value.size()) {
      return std::nullopt;
    }
    return number;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::size_t> extract_list_size(const std::string& payload,
                                                           const std::string_view& field_path) {
  const auto token = find_field_token_in_object(payload, field_path);
  if (!token.has_value() || token->kind != JsonTokenKind::Array) {
    return std::nullopt;
  }

  const auto trimmed = trim_view(token->raw);
  if (trimmed.size() < 2U || trimmed.front() != '[' || trimmed.back() != ']') {
    return std::nullopt;
  }

  std::size_t cursor = 1U;
  std::size_t item_count = 0U;
  while (cursor + 1U <= trimmed.size()) {
    skip_ascii_ws(trimmed, cursor);
    if (cursor >= trimmed.size() - 1U) {
      return item_count;
    }

    const auto item = parse_json_value_token(trimmed, cursor);
    if (!item.has_value()) {
      return std::nullopt;
    }
    ++item_count;

    skip_ascii_ws(trimmed, cursor);
    if (cursor >= trimmed.size() - 1U) {
      return item_count;
    }
    if (trimmed[cursor] != ',') {
      return std::nullopt;
    }
    ++cursor;
  }

  return item_count;
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
  if (!parse_json_object_fields(payload).has_value()) {
    issue_set.add(ValidationIssueCode::MalformedJson,
                  "response.content_payload",
                  "stage output must be a well-formed JSON object");
    return finalize_result(std::move(issue_set), schema_spec.stage_name, std::move(diagnostics));
  }

  for (const auto& field_path : schema_spec.required_fields) {
    if (!find_field_token_in_object(payload, field_path).has_value()) {
      issue_set.add(ValidationIssueCode::MissingRequiredField,
                    field_path,
                    std::string("missing required field: ") + field_path);
    }
  }

  for (const auto& enum_constraint : schema_spec.enum_constraints) {
    const auto value = extract_string_field(payload, enum_constraint.field_path);
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
    const auto value = extract_number_field(payload, numeric_constraint.field_path);
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
    const auto list_size = extract_list_size(payload, list_constraint.field_path);
    if (!list_size.has_value()) {
      issue_set.add(ValidationIssueCode::ListSizeOutOfRange,
                    list_constraint.field_path,
                    std::string("list field missing or invalid: ") +
                        list_constraint.field_path);
      continue;
    }
    if (*list_size < list_constraint.min_items ||
        (list_constraint.max_items.has_value() && *list_size > *list_constraint.max_items)) {
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

  switch (action_decision.decision_kind) {
    case decision::ActionDecisionKind::ExecuteAction:
      if (!action_decision.selected_node_id.has_value() || action_decision.selected_node_id->empty()) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "selected_node_id",
                      "execute_action decisions require a selected plan node");
      }
      if (!action_decision.tool_intent_hint.has_value() ||
          action_decision.tool_intent_hint->tool_name.empty()) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "tool_intent_hint.tool_name",
                      "execute_action decisions require a tool intent hint");
      }
      break;
    case decision::ActionDecisionKind::DirectResponse:
    case decision::ActionDecisionKind::ConvergeSafe:
      if (!action_decision.response_outline.has_value() ||
          action_decision.response_outline->summary.empty()) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "response_outline.summary",
                      "response decisions require a non-empty response outline summary");
      }
      break;
    case decision::ActionDecisionKind::AskClarification:
      if (!action_decision.clarification_needed ||
          !action_decision.clarification_question.has_value() ||
          action_decision.clarification_question->empty()) {
        issue_set.add(ValidationIssueCode::ActionDecisionInvariant,
                      "clarification_question",
                      "clarification decisions require a clarification question");
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
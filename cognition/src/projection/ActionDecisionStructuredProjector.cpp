#include "projection/ActionDecisionStructuredProjector.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dasall::cognition::projection {
namespace {

using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::validation::JsonTokenKind;
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
          .stage = "execution",
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition.action_decision_structured_projector",
          .ref_id = std::move(field_path),
      },
  };
}

[[nodiscard]] ActionDecisionProjectionResult make_projection_failure(std::string field_path,
                                                                     std::string message,
                                                                     std::string diagnostic) {
  ActionDecisionProjectionResult result;
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

[[nodiscard]] std::optional<std::optional<std::string>> read_nullable_string(
    const StructuredPayloadView& object_view,
    std::string_view field_path) {
  const auto token = object_view.field_token(field_path);
  if (!token.has_value() || token->kind == JsonTokenKind::Null) {
    return std::optional<std::string>{};
  }
  if (token->kind != JsonTokenKind::String) {
    return std::nullopt;
  }

  const auto value = object_view.read_string(field_path);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return std::optional<std::string>(*value);
}

[[nodiscard]] std::optional<ActionDecisionKind> parse_decision_kind(std::string_view literal) {
  if (literal == "ExecuteAction") {
    return ActionDecisionKind::ExecuteAction;
  }
  if (literal == "DirectResponse") {
    return ActionDecisionKind::DirectResponse;
  }
  if (literal == "AskClarification") {
    return ActionDecisionKind::AskClarification;
  }
  if (literal == "ConvergeSafe") {
    return ActionDecisionKind::ConvergeSafe;
  }
  if (literal == "NoDecision") {
    return ActionDecisionKind::NoDecision;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<decision::ToolIntentHint> project_tool_intent_hint(
    const StructuredPayloadView& object_view) {
  const auto tool_name = read_required_string(object_view, "tool_name");
  const auto intent_summary = read_required_string(object_view, "intent_summary");
  if (!tool_name.has_value() || !intent_summary.has_value()) {
    return std::nullopt;
  }

  decision::ToolIntentHint hint;
  hint.tool_name = *tool_name;
  hint.intent_summary = *intent_summary;

  const auto argument_hints = read_string_list(object_view, "argument_hints");
  if (!argument_hints.has_value()) {
    return std::nullopt;
  }
  hint.argument_hints = *argument_hints;

  const auto evidence_refs = read_string_list(object_view, "evidence_refs");
  if (!evidence_refs.has_value()) {
    return std::nullopt;
  }
  hint.evidence_refs = *evidence_refs;
  return hint;
}

[[nodiscard]] std::optional<decision::ResponseOutline> project_response_outline(
    const StructuredPayloadView& object_view) {
  const auto summary = read_required_string(object_view, "summary");
  if (!summary.has_value()) {
    return std::nullopt;
  }

  decision::ResponseOutline outline;
  outline.summary = *summary;

  const auto key_points = read_string_list(object_view, "key_points");
  if (!key_points.has_value()) {
    return std::nullopt;
  }
  outline.key_points = *key_points;
  return outline;
}

[[nodiscard]] std::optional<decision::CandidateDecisionScore> project_candidate_score(
    const StructuredPayloadView& object_view) {
  const auto candidate_name = read_required_string(object_view, "candidate_name");
  if (!candidate_name.has_value()) {
    return std::nullopt;
  }

  const auto score = object_view.read_number("score");
  if (!score.has_value() || *score < 0.0 || *score > 1.0) {
    return std::nullopt;
  }

  const auto rationale = read_nullable_string(object_view, "rationale");
  if (!rationale.has_value()) {
    return std::nullopt;
  }

  decision::CandidateDecisionScore candidate_score;
  candidate_score.candidate_name = *candidate_name;
  candidate_score.score = static_cast<float>(*score);
  candidate_score.rationale = *rationale;
  return candidate_score;
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

template <typename T, typename ProjectFn>
[[nodiscard]] std::optional<std::optional<T>> read_nullable_object(
    const StructuredPayloadView& object_view,
    std::string_view field_path,
    ProjectFn&& project_fn) {
  const auto token = object_view.field_token(field_path);
  if (!token.has_value() || token->kind == JsonTokenKind::Null) {
    return std::optional<T>{};
  }
  if (token->kind != JsonTokenKind::Object) {
    return std::nullopt;
  }

  const auto nested_view = object_view.read_object(field_path);
  if (!nested_view.has_value()) {
    return std::nullopt;
  }

  const auto projected = project_fn(*nested_view);
  if (!projected.has_value()) {
    return std::nullopt;
  }
  return std::optional<T>(*projected);
}

}  // namespace

ActionDecisionProjectionResult ActionDecisionStructuredProjector::project_action_decision(
    const StructuredPayloadView& payload_view) const {
  const auto decision_kind_literal = read_required_string(payload_view, "decision_kind");
  if (!decision_kind_literal.has_value()) {
    return make_projection_failure(
        "decision_kind",
        "execution payload must contain a non-empty decision_kind",
        "structured_output.projection_failed:execution:decision_kind");
  }

  const auto decision_kind = parse_decision_kind(*decision_kind_literal);
  if (!decision_kind.has_value()) {
    return make_projection_failure(
        "decision_kind",
        "execution payload decision_kind must map to a supported ActionDecisionKind",
        "structured_output.projection_failed:execution:decision_kind");
  }

  const auto confidence = payload_view.read_number("confidence");
  if (!confidence.has_value() || *confidence < 0.0 || *confidence > 1.0) {
    return make_projection_failure(
        "confidence",
        "execution payload must contain confidence in the range [0.0, 1.0]",
        "structured_output.projection_failed:execution:confidence");
  }

  const auto clarification_needed = payload_view.read_bool("clarification_needed");
  if (!clarification_needed.has_value()) {
    return make_projection_failure(
        "clarification_needed",
        "execution payload must contain a boolean clarification_needed field",
        "structured_output.projection_failed:execution:clarification_needed");
  }

  const auto candidate_scores = project_object_array<decision::CandidateDecisionScore>(
      payload_view,
      "candidate_scores",
      [](const StructuredPayloadView& score_view) { return project_candidate_score(score_view); });
  if (!candidate_scores.has_value() || candidate_scores->empty() || candidate_scores->size() > 4U) {
    return make_projection_failure(
        "candidate_scores",
        "execution payload candidate_scores must contain 1 to 4 structured score objects",
        "structured_output.projection_failed:execution:candidate_scores");
  }

  const auto rationale = read_nullable_string(payload_view, "rationale");
  if (!rationale.has_value()) {
    return make_projection_failure(
        "rationale",
        "execution payload rationale must be a string or null",
        "structured_output.projection_failed:execution:rationale");
  }

  const auto selected_node_id = read_nullable_string(payload_view, "selected_node_id");
  if (!selected_node_id.has_value()) {
    return make_projection_failure(
        "selected_node_id",
        "execution payload selected_node_id must be a string or null",
        "structured_output.projection_failed:execution:selected_node_id");
  }

  const auto clarification_question = read_nullable_string(payload_view, "clarification_question");
  if (!clarification_question.has_value()) {
    return make_projection_failure(
        "clarification_question",
        "execution payload clarification_question must be a string or null",
        "structured_output.projection_failed:execution:clarification_question");
  }

  const auto tool_intent_hint = read_nullable_object<decision::ToolIntentHint>(
      payload_view,
      "tool_intent_hint",
      [](const StructuredPayloadView& tool_view) { return project_tool_intent_hint(tool_view); });
  if (!tool_intent_hint.has_value()) {
    return make_projection_failure(
        "tool_intent_hint",
        "execution payload tool_intent_hint must be null or a valid structured object",
        "structured_output.projection_failed:execution:tool_intent_hint");
  }

  const auto response_outline = read_nullable_object<decision::ResponseOutline>(
      payload_view,
      "response_outline",
      [](const StructuredPayloadView& outline_view) {
        return project_response_outline(outline_view);
      });
  if (!response_outline.has_value()) {
    return make_projection_failure(
        "response_outline",
        "execution payload response_outline must be null or a valid structured object",
        "structured_output.projection_failed:execution:response_outline");
  }

  decision::ActionDecision action_decision;
  action_decision.decision_kind = *decision_kind;
  action_decision.selected_node_id = *selected_node_id;
  action_decision.rationale = *rationale;
  action_decision.confidence = static_cast<float>(*confidence);
  action_decision.clarification_needed = *clarification_needed;
  action_decision.clarification_question = *clarification_question;
  action_decision.tool_intent_hint = *tool_intent_hint;
  action_decision.delegate_hint = std::nullopt;
  action_decision.response_outline = *response_outline;
  action_decision.candidate_scores = *candidate_scores;

  ActionDecisionProjectionResult result;
  result.ok = true;
  result.action_decision = std::move(action_decision);
  result.diagnostics.push_back("structured_output.projected:execution");
  return result;
}

}  // namespace dasall::cognition::projection
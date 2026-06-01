#include "reasoning/DecisionProjector.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace dasall::cognition::reasoning {
namespace {

template <typename T>
[[nodiscard]] const std::vector<T>& optional_vector_or_empty(
    const std::optional<std::vector<T>>& value) {
  static const std::vector<T> kEmpty;
  return value.has_value() ? *value : kEmpty;
}

[[nodiscard]] std::string optional_string_or(const std::optional<std::string>& value,
                                             std::string_view fallback) {
  return value.has_value() ? *value : std::string(fallback);
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

[[nodiscard]] std::string first_tool_name(const ReasoningRequest& request) {
  for (const auto& entity : request.perception_result.entities) {
    if (entity.name == "tool" && !entity.value.empty()) {
      return entity.value;
    }
  }

  const auto& active_tools = optional_vector_or_empty(request.context_packet.active_tools);
  return active_tools.empty() ? std::string("planner.next_step") : active_tools.front();
}

inline constexpr std::size_t kToolCandidateTopK = 3U;

[[nodiscard]] std::string lowercase_copy(std::string_view value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    normalized.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  }
  return normalized;
}

[[nodiscard]] bool contains_case_insensitive(std::string_view haystack,
                                             std::string_view needle) {
  if (needle.empty()) {
    return false;
  }

  return lowercase_copy(haystack).find(lowercase_copy(needle)) != std::string::npos;
}

void append_tokenized_words(std::vector<std::string>& target, std::string_view value) {
  std::string token;

  const auto flush_token = [&target, &token]() {
    if (token.size() >= 3U) {
      target.push_back(token);
    }
    token.clear();
  };

  for (const char ch : value) {
    const auto lower = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
    if (std::isalnum(static_cast<unsigned char>(lower))) {
      token.push_back(lower);
      continue;
    }

    flush_token();
  }

  flush_token();
}

[[nodiscard]] std::vector<std::string> collect_prefilter_keywords(
    const ReasoningRequest& request,
    const plan::PlanNode& active_node) {
  std::vector<std::string> keywords;
  append_tokenized_words(keywords, active_node.objective);
  append_tokenized_words(keywords, active_node.success_signal);
  append_tokenized_words(
      keywords,
      optional_string_or(request.goal_contract.goal_description, std::string_view{}));
  append_tokenized_words(
      keywords,
      optional_string_or(request.goal_contract.success_criteria, std::string_view{}));
  append_tokenized_words(keywords, request.perception_result.intent_summary);
  append_tokenized_words(
      keywords,
      optional_string_or(request.context_packet.user_turn, std::string_view{}));
  append_tokenized_words(
      keywords,
      optional_string_or(request.context_packet.current_goal_summary, std::string_view{}));

  for (const auto& entity : request.perception_result.entities) {
    append_tokenized_words(keywords, entity.name);
    append_tokenized_words(keywords, entity.value);
  }

  std::vector<std::string> deduped_keywords;
  deduped_keywords.reserve(keywords.size());
  std::unordered_set<std::string> seen;
  for (const auto& keyword : keywords) {
    if (seen.insert(keyword).second) {
      deduped_keywords.push_back(keyword);
    }
  }

  return deduped_keywords;
}

struct RankedToolDescriptor {
  const contracts::ToolDescriptor* descriptor = nullptr;
  int score = 0;
};

[[nodiscard]] int score_tool_descriptor(
    const contracts::ToolDescriptor& descriptor,
    const ReasoningRequest& request,
    const plan::PlanNode& active_node,
    const std::vector<std::string>& keywords) {
  if (!descriptor.tool_name.has_value() || descriptor.tool_name->empty()) {
    return 0;
  }

  const auto& tool_name = *descriptor.tool_name;
  int score = 0;

  for (const auto& entity : request.perception_result.entities) {
    if (entity.name == "tool" && entity.value == tool_name) {
      score += 120;
      break;
    }
  }

  const auto& active_tools = optional_vector_or_empty(request.context_packet.active_tools);
  if (std::find(active_tools.begin(), active_tools.end(), tool_name) != active_tools.end()) {
    score += 60;
  }

  if (contains_case_insensitive(active_node.objective, tool_name) ||
      contains_case_insensitive(request.perception_result.intent_summary, tool_name) ||
      contains_case_insensitive(
          optional_string_or(request.goal_contract.goal_description, std::string_view{}),
          tool_name) ||
      contains_case_insensitive(
          optional_string_or(request.context_packet.user_turn, std::string_view{}),
          tool_name)) {
    score += 25;
  }

  for (const auto& keyword : keywords) {
    if (contains_case_insensitive(tool_name, keyword)) {
      score += 12;
    }

    if (descriptor.display_name.has_value() &&
        contains_case_insensitive(*descriptor.display_name, keyword)) {
      score += 8;
    }

    const auto& tags = optional_vector_or_empty(descriptor.tags);
    for (const auto& tag : tags) {
      if (contains_case_insensitive(tag, keyword)) {
        score += 6;
      }
    }
  }

  if (descriptor.category.has_value() &&
      *descriptor.category == contracts::ToolCategory::Information &&
      active_node.action_kind_hint == "tool_execution") {
    score += 3;
  }

  return score;
}

[[nodiscard]] std::vector<RankedToolDescriptor> prefilter_tool_descriptors(
    const ReasoningRequest& request,
    const plan::PlanNode& active_node) {
  if (request.available_tool_descriptors.empty()) {
    return {};
  }

  const auto keywords = collect_prefilter_keywords(request, active_node);
  std::vector<RankedToolDescriptor> ranked_descriptors;
  ranked_descriptors.reserve(request.available_tool_descriptors.size());

  for (const auto& descriptor : request.available_tool_descriptors) {
    const int score = score_tool_descriptor(descriptor, request, active_node, keywords);
    if (score <= 0) {
      continue;
    }

    ranked_descriptors.push_back(RankedToolDescriptor{
        .descriptor = &descriptor,
        .score = score,
    });
  }

  std::sort(ranked_descriptors.begin(), ranked_descriptors.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }

    const auto& lhs_name = lhs.descriptor->tool_name.value_or(std::string{});
    const auto& rhs_name = rhs.descriptor->tool_name.value_or(std::string{});
    return lhs_name < rhs_name;
  });

  if (ranked_descriptors.size() > kToolCandidateTopK) {
    ranked_descriptors.resize(kToolCandidateTopK);
  }

  return ranked_descriptors;
}

}  // namespace

DecisionProjector::DecisionProjector(CognitionConfig config)
    : config_(std::move(config)) {}

decision::ActionDecision DecisionProjector::build_execute_action_decision(
    const ReasoningRequest& request,
    const plan::PlanNode& active_node,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  auto tool_intent_projection = build_tool_intent_hint(request, active_node);
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::ExecuteAction,
      .selected_node_id = active_node.node_id,
      .rationale = std::string("highest-scoring actionable node selected for execution"),
      .confidence = confidence,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .tool_intent_hint = std::move(tool_intent_projection.hint),
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(request, "execute", active_node.objective),
      .candidate_scores = std::move(candidate_scores),
      .diagnostics = std::move(tool_intent_projection.diagnostics),
  };
}

decision::ActionDecision DecisionProjector::build_direct_response_decision(
    const ReasoningRequest& request,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::DirectResponse,
      .selected_node_id = std::nullopt,
      .rationale = std::string("direct response scored above the response threshold"),
      .confidence = confidence,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .tool_intent_hint = std::nullopt,
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(
          request, "direct_response",
          optional_string_or(request.goal_contract.goal_description, "current goal")),
      .candidate_scores = std::move(candidate_scores),
        .diagnostics = {},
  };
}

decision::ActionDecision DecisionProjector::build_clarification_decision(
    const ReasoningRequest& request,
    std::string clarification_question,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::AskClarification,
      .selected_node_id = std::nullopt,
      .rationale = std::string("clarification is required before safe execution can continue"),
      .confidence = confidence,
      .clarification_needed = true,
      .clarification_question = std::move(clarification_question),
      .tool_intent_hint = std::nullopt,
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(
          request, "clarify",
          optional_string_or(request.goal_contract.goal_description, "current goal")),
      .candidate_scores = std::move(candidate_scores),
        .diagnostics = {},
  };
}

decision::ActionDecision DecisionProjector::build_converge_safe_decision(
    const ReasoningRequest& request,
    std::string rationale,
    float confidence,
    std::vector<decision::CandidateDecisionScore> candidate_scores) const {
  return decision::ActionDecision{
      .decision_kind = decision::ActionDecisionKind::ConvergeSafe,
      .selected_node_id = std::nullopt,
      .rationale = std::move(rationale),
      .confidence = confidence,
      .clarification_needed = false,
      .clarification_question = std::nullopt,
      .tool_intent_hint = std::nullopt,
      .delegate_hint = std::nullopt,
      .response_outline = project_response_outline(
          request, "converge_safe",
          optional_string_or(request.goal_contract.success_criteria, "safe completion")),
      .candidate_scores = std::move(candidate_scores),
        .diagnostics = {},
  };
}

decision::ResponseOutline DecisionProjector::project_response_outline(
    const ReasoningRequest& request,
    std::string_view mode,
    std::string_view focus) const {
  decision::ResponseOutline outline;
  outline.summary = std::string(mode) + ": " + std::string(focus);
  outline.key_points.push_back(optional_string_or(
      request.goal_contract.goal_description, "goal description unavailable"));
  outline.key_points.push_back(optional_string_or(
      request.goal_contract.success_criteria, "success criteria unavailable"));

  if (request.latest_observation.has_value() &&
      request.latest_observation->payload.has_value() &&
      !request.latest_observation->payload->empty()) {
    outline.key_points.push_back(*request.latest_observation->payload);
  }

  return outline;
}

DecisionProjector::ToolIntentProjection DecisionProjector::build_tool_intent_hint(
    const ReasoningRequest& request,
    const plan::PlanNode& active_node) const {
  ToolIntentProjection projection;
  decision::ToolIntentHint hint;

  const auto ranked_descriptors = prefilter_tool_descriptors(request, active_node);
  if (!ranked_descriptors.empty()) {
    hint.tool_name = ranked_descriptors.front().descriptor->tool_name.value_or(first_tool_name(request));
    projection.diagnostics.push_back("tool_candidate_prefilter:applied");
    projection.diagnostics.push_back(
        "tool_candidate_prefilter_count:" + std::to_string(ranked_descriptors.size()));
  } else {
    hint.tool_name = first_tool_name(request);
    if (!request.available_tool_descriptors.empty()) {
      projection.diagnostics.push_back("tool_candidate_prefilter:fallback");
    }
  }

  hint.intent_summary = active_node.objective;

  if (request.goal_contract.goal_description.has_value() &&
      !request.goal_contract.goal_description->empty()) {
    hint.argument_hints.push_back(*request.goal_contract.goal_description);
  }
  if (request.goal_contract.success_criteria.has_value() &&
      !request.goal_contract.success_criteria->empty()) {
    hint.argument_hints.push_back(*request.goal_contract.success_criteria);
  }

  append_unique(hint.evidence_refs, active_node.evidence_refs);
  append_unique(hint.evidence_refs,
                optional_vector_or_empty(request.belief_state.evidence_refs));
  projection.hint = std::move(hint);
  return projection;
}

}  // namespace dasall::cognition::reasoning
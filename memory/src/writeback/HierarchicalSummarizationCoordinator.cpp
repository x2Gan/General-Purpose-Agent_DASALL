#include "writeback/HierarchicalSummarizationCoordinator.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace dasall::memory {
namespace {

std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

template <typename T>
void append_unique(std::vector<T>& destination, const T& value) {
  if (std::find(destination.begin(), destination.end(), value) ==
      destination.end()) {
    destination.push_back(value);
  }
}

std::string join_strings(const std::vector<std::string>& values,
                         const std::string& separator) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      stream << separator;
    }
    stream << values[index];
  }
  return stream.str();
}

std::string summarize_child_page(const contracts::SummaryMemory& summary) {
  std::vector<std::string> parts;
  if (summary.summary_text.has_value() && !summary.summary_text->empty()) {
    parts.push_back(*summary.summary_text);
  }
  if (summary.decisions_made.has_value() && !summary.decisions_made->empty()) {
    parts.push_back("Decisions: " + join_strings(*summary.decisions_made, " | "));
  }
  if (summary.confirmed_facts.has_value() && !summary.confirmed_facts->empty()) {
    parts.push_back("Facts: " + join_strings(*summary.confirmed_facts, " | "));
  }
  if (summary.tool_outcomes.has_value() && !summary.tool_outcomes->empty()) {
    parts.push_back("Tool outcomes: " + join_strings(*summary.tool_outcomes, " | "));
  }
  return join_strings(parts, " ");
}

}  // namespace

HierarchicalSummarizationCoordinator::HierarchicalSummarizationCoordinator(
    ISessionStore& session_store,
    ISummaryStore& summary_store,
    const MemoryConfig& config,
    ISummarizer* summarizer,
    std::shared_ptr<const util::ITokenEstimator> token_estimator)
    : session_store_(session_store),
      summary_store_(summary_store),
      compression_coordinator_(summary_store, summarizer, token_estimator),
      hierarchy_config_(config.compression.hierarchy) {}

HierarchicalSummarizationResult
HierarchicalSummarizationCoordinator::promote_from_summary(
    const contracts::SummaryMemory& summary) {
  HierarchicalSummarizationResult result;
  if (!hierarchy_config_.enabled || !summary.session_id.has_value() ||
      summary.session_id->empty()) {
    return result;
  }

  const auto source_level =
      extract_summary_level(summary).value_or(HierarchicalSummaryLevel::Dialog);
  const auto target_level = next_hierarchical_summary_level(source_level);
  if (!target_level.has_value()) {
    return result;
  }

  const int threshold = threshold_for(source_level);
  if (threshold <= 1) {
    return result;
  }

  const auto source_summaries = summary_store_.load_unparented_summaries(
      *summary.session_id, source_level, static_cast<std::size_t>(threshold));
  if (static_cast<int>(source_summaries.size()) < threshold) {
    return result;
  }

  auto promote_result = promote_once(HierarchicalSummaryRequest{
      .session_id = *summary.session_id,
      .source_level = source_level,
      .target_level = *target_level,
      .source_summaries = source_summaries,
      .user_id = resolve_user_id(*summary.session_id),
      .target_token_budget = target_budget_for(*target_level),
  });
  result = std::move(promote_result);

  if (result.promoted && result.promoted_summary.has_value()) {
    auto cascade_result = promote_from_summary(*result.promoted_summary);
    for (const auto& warning : cascade_result.warnings) {
      append_unique(result.warnings, warning);
    }
  }

  return result;
}

int HierarchicalSummarizationCoordinator::threshold_for(
    HierarchicalSummaryLevel source_level) const {
  switch (source_level) {
    case HierarchicalSummaryLevel::Dialog:
      return hierarchy_config_.dialog_to_topic_threshold;
    case HierarchicalSummaryLevel::Topic:
      return hierarchy_config_.topic_to_profile_threshold;
    case HierarchicalSummaryLevel::Profile:
      return 0;
  }
  return 0;
}

std::optional<std::string> HierarchicalSummarizationCoordinator::resolve_user_id(
    const std::string& session_id) const {
  const auto bundle = session_store_.load_session_bundle(
      SessionLoadRequest{.session_id = session_id, .recent_turn_limit = 0});
  if (!bundle.session.user_id.has_value() || bundle.session.user_id->empty()) {
    return std::nullopt;
  }
  return bundle.session.user_id;
}

std::vector<contracts::Turn>
HierarchicalSummarizationCoordinator::build_turns_from_summaries(
    const HierarchicalSummaryRequest& request) const {
  std::vector<contracts::Turn> turns;
  turns.reserve(request.source_summaries.size());
  for (const auto& source_summary : request.source_summaries) {
    contracts::Turn turn;
    turn.turn_id = source_summary.summary_id;
    turn.session_id = request.session_id;
    turn.user_input =
        std::string{"hierarchy-source:"} +
        std::string{to_string_view(request.source_level)};
    turn.agent_response = summarize_child_page(source_summary);
    turn.created_at = source_summary.created_at.value_or(current_time_ms());
    turns.push_back(std::move(turn));
  }
  return turns;
}

std::string HierarchicalSummarizationCoordinator::build_parent_summary_id(
    const HierarchicalSummaryRequest& request) const {
  const auto suffix = request.source_summaries.empty()
                          ? std::string{"page"}
                          : request.source_summaries.back().summary_id.value_or("page");
  return request.session_id + "-" + std::string{to_string_view(request.target_level)} +
         "-summary-" + suffix;
}

int HierarchicalSummarizationCoordinator::target_budget_for(
    HierarchicalSummaryLevel target_level) const {
  switch (target_level) {
    case HierarchicalSummaryLevel::Topic:
      return 384;
    case HierarchicalSummaryLevel::Profile:
      return 512;
    case HierarchicalSummaryLevel::Dialog:
      return 256;
  }
  return 256;
}

HierarchicalSummarizationResult
HierarchicalSummarizationCoordinator::promote_once(
    const HierarchicalSummaryRequest& request) {
  HierarchicalSummarizationResult result;
  if (request.source_summaries.empty()) {
    return result;
  }

  const auto compression_output = compression_coordinator_.compress(CompressionInput{
      .session_id = request.session_id,
      .source_turns = build_turns_from_summaries(request),
      .existing_summary = std::nullopt,
      .target_token_budget = request.target_token_budget,
      .materialize_latest_summary = false,
      .strategy_hint = std::string{"hierarchy:"} +
                       std::string{to_string_view(request.target_level)},
  });
  if (!compression_output.compression_applied) {
    append_unique(result.warnings, std::string{"hierarchy_compression_skipped"});
    return result;
  }

  auto parent_summary = compression_output.summary;
  parent_summary.summary_id = build_parent_summary_id(request);
  parent_summary.session_id = request.session_id;
  parent_summary.created_at = current_time_ms();
  parent_summary.source_turn_ids = std::vector<std::string>{};
  for (const auto& source_summary : request.source_summaries) {
    if (source_summary.summary_id.has_value() && !source_summary.summary_id->empty()) {
      parent_summary.source_turn_ids->push_back(*source_summary.summary_id);
    }
  }
  ensure_summary_level_tag(parent_summary, request.target_level);
  if (!parent_summary.tags.has_value()) {
    parent_summary.tags = std::vector<std::string>{};
  }
  append_unique(*parent_summary.tags, std::string{"hierarchical"});
  append_unique(*parent_summary.tags,
                std::string{"hierarchy_source:"} +
                    std::string{to_string_view(request.source_level)});
  if (request.user_id.has_value() && !request.user_id->empty()) {
    append_unique(*parent_summary.tags,
                  std::string{"summary_owner_user:"} + *request.user_id);
  }

  const auto upsert_result = summary_store_.upsert_summary(parent_summary);
  if (!upsert_result.ok) {
    append_unique(result.warnings, std::string{"hierarchy_parent_upsert_failed"});
    return result;
  }

  std::vector<std::string> child_summary_ids;
  child_summary_ids.reserve(request.source_summaries.size());
  for (const auto& source_summary : request.source_summaries) {
    if (source_summary.summary_id.has_value() && !source_summary.summary_id->empty()) {
      child_summary_ids.push_back(*source_summary.summary_id);
    }
  }

  const auto reparent_result = summary_store_.assign_summary_parent(
      child_summary_ids, *parent_summary.summary_id);
  if (!reparent_result.ok) {
    append_unique(result.warnings, std::string{"hierarchy_parent_assignment_failed"});
    return result;
  }

  result.promoted_summary = std::move(parent_summary);
  result.promoted = true;
  return result;
}

}  // namespace dasall::memory
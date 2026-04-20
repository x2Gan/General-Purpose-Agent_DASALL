#include "writeback/CompressionCoordinator.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string_view>

#include "util/TokenEstimator.h"

namespace dasall::memory {
namespace {

using util::estimate_text_tokens;

std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

template <typename T>
void append_unique(std::vector<T>& destination, const T& value) {
  if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
    destination.push_back(value);
  }
}

void merge_unique(std::vector<std::string>& destination,
                  const std::vector<std::string>& source) {
  for (const auto& value : source) {
    append_unique(destination, value);
  }
}

std::string join_strings(const std::vector<std::string>& values,
                         std::string_view separator) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      stream << separator;
    }
    stream << values[index];
  }
  return stream.str();
}

bool contains_any_keyword(std::string_view text,
                          std::initializer_list<std::string_view> keywords) {
  return std::any_of(keywords.begin(), keywords.end(),
                     [text](std::string_view keyword) {
                       return !keyword.empty() && text.find(keyword) != std::string_view::npos;
                     });
}

int estimate_projection_tokens(const SummaryProjection& projection) {
  int total = estimate_text_tokens(projection.summary_text);
  for (const auto& decision : projection.decisions_made) {
    total += estimate_text_tokens(decision);
  }
  for (const auto& fact : projection.confirmed_facts) {
    total += estimate_text_tokens(fact);
  }
  for (const auto& outcome : projection.tool_outcomes) {
    total += estimate_text_tokens(outcome);
  }
  for (const auto& turn_id : projection.source_turn_ids) {
    total += estimate_text_tokens(turn_id);
  }
  if (total <= 0) {
    return 0;
  }
  return total + std::max(1, (total + 9) / 10);
}

std::string build_summary_id(const CompressionInput& input) {
  if (input.existing_summary.has_value() &&
      input.existing_summary->summary_id.has_value() &&
      !input.existing_summary->summary_id->empty()) {
    return *input.existing_summary->summary_id;
  }

  if (!input.source_turns.empty() && input.source_turns.back().turn_id.has_value() &&
      !input.source_turns.back().turn_id->empty()) {
    return input.session_id + "-summary-" + *input.source_turns.back().turn_id;
  }

  return input.session_id + "-summary";
}

}  // namespace

CompressionCoordinator::CompressionCoordinator(ISummaryStore& store,
                                               ISummarizer* summarizer)
    : store_(store), summarizer_(summarizer) {}

CompressionOutput CompressionCoordinator::compress(const CompressionInput& input) {
  CompressionOutput output;
  if (input.source_turns.empty()) {
    return output;
  }

  SummaryProjection projection;
  bool use_template_projection = (summarizer_ == nullptr);

  if (summarizer_ != nullptr) {
    try {
      const auto generation_result = summarizer_->summarize(SummaryGenerationRequest{
          .session_id = input.session_id,
          .source_turns = input.source_turns,
          .existing_summary = input.existing_summary,
          .target_token_budget = input.target_token_budget,
          .strategy_hint = input.strategy_hint,
      });
      projection = generation_result.projection;
      merge_unique(output.compression_notes, generation_result.warnings);
      use_template_projection = generation_result.fallback_used ||
                                generation_result.degraded ||
                                projection.summary_text.empty();
    } catch (...) {
      use_template_projection = true;
      append_unique(output.compression_notes, std::string{"summarizer_fallback"});
    }
  }

  if (use_template_projection) {
    if (summarizer_ != nullptr) {
      append_unique(output.compression_notes, std::string{"summarizer_fallback"});
    }
    projection = extract_structured_summary(input);
  }

  projection = merge_with_existing(input.existing_summary, std::move(projection));
  if (projection.summary_text.empty()) {
    projection.summary_text = build_summary_text(input, projection);
  }
  projection.estimated_tokens = estimate_projection_tokens(projection);

  output.projection = projection;
  output.summary = materialize_summary(input, output.projection);
  output.compression_applied = !output.projection.summary_text.empty();

  append_unique(output.compression_notes,
                std::string{"strategy:"} + (use_template_projection ? "template" : "summarizer"));

  if (input.materialize_latest_summary && output.compression_applied) {
    const auto materialize_result = store_.upsert_summary(output.summary);
    if (materialize_result.ok) {
      append_unique(output.compression_notes, std::string{"summary_materialized"});
    } else {
      append_unique(output.compression_notes, std::string{"summary_materialization_failed"});
    }
  }

  return output;
}

SummaryProjection CompressionCoordinator::extract_structured_summary(
    const CompressionInput& input) const {
  SummaryProjection projection;
  projection.decisions_made = extract_decisions(input.source_turns);
  projection.confirmed_facts = extract_confirmed_facts(input.source_turns);
  projection.tool_outcomes = extract_tool_outcomes(input.source_turns);

  for (const auto& turn : input.source_turns) {
    if (turn.turn_id.has_value() && !turn.turn_id->empty()) {
      append_unique(projection.source_turn_ids, *turn.turn_id);
    }
  }

  projection.summary_text = build_summary_text(input, projection);
  projection.estimated_tokens = estimate_projection_tokens(projection);
  return projection;
}

std::vector<std::string> CompressionCoordinator::extract_decisions(
    const std::vector<contracts::Turn>& turns) const {
  std::vector<std::string> decisions;
  for (const auto& turn : turns) {
    if (turn.agent_response.has_value() &&
        contains_any_keyword(*turn.agent_response,
                             {"决定", "计划", "下一步", "decide", "plan"})) {
      append_unique(decisions, *turn.agent_response);
    }
    if (turn.user_input.has_value() &&
        contains_any_keyword(*turn.user_input,
                             {"下一步", "计划", "需要"})) {
      append_unique(decisions, *turn.user_input);
    }
  }
  return decisions;
}

std::vector<std::string> CompressionCoordinator::extract_confirmed_facts(
    const std::vector<contracts::Turn>& turns) const {
  std::vector<std::string> facts;
  for (const auto& turn : turns) {
    if (turn.user_input.has_value() &&
        contains_any_keyword(*turn.user_input,
                             {"确认", "必须", "需要", "confirmed", "must"})) {
      append_unique(facts, *turn.user_input);
    }
    if (turn.agent_response.has_value() &&
        contains_any_keyword(*turn.agent_response,
                             {"已完成", "确认", "完成", "confirmed", "completed"})) {
      append_unique(facts, *turn.agent_response);
    }
  }
  return facts;
}

std::vector<std::string> CompressionCoordinator::extract_tool_outcomes(
    const std::vector<contracts::Turn>& turns) const {
  std::vector<std::string> tool_outcomes;
  for (const auto& turn : turns) {
    if (turn.tool_call_refs.has_value() && !turn.tool_call_refs->empty()) {
      append_unique(tool_outcomes,
                    std::string{"tool_calls: "} + join_strings(*turn.tool_call_refs, ", "));
    }
    if (turn.observation_refs.has_value() && !turn.observation_refs->empty()) {
      append_unique(tool_outcomes,
                    std::string{"observations: "} +
                        join_strings(*turn.observation_refs, ", "));
    }
    if (turn.agent_response.has_value() &&
        contains_any_keyword(*turn.agent_response,
                             {"成功", "失败", "完成", "success", "failed"})) {
      append_unique(tool_outcomes, *turn.agent_response);
    }
  }
  return tool_outcomes;
}

std::string CompressionCoordinator::build_summary_text(
    const CompressionInput& input,
    const SummaryProjection& projection) const {
  std::vector<std::string> parts;
  parts.push_back("Session " + input.session_id + " compressed " +
                  std::to_string(input.source_turns.size()) + " turns.");

  if (!input.source_turns.empty()) {
    const auto& latest_turn = input.source_turns.back();
    if (latest_turn.user_input.has_value() && !latest_turn.user_input->empty()) {
      parts.push_back("Latest user input: " + *latest_turn.user_input);
    }
    if (latest_turn.agent_response.has_value() && !latest_turn.agent_response->empty()) {
      parts.push_back("Latest response: " + *latest_turn.agent_response);
    }
  }

  if (!projection.decisions_made.empty()) {
    parts.push_back("Decisions: " + join_strings(projection.decisions_made, " | "));
  }
  if (!projection.confirmed_facts.empty()) {
    parts.push_back("Confirmed facts: " + join_strings(projection.confirmed_facts, " | "));
  }
  if (!projection.tool_outcomes.empty()) {
    parts.push_back("Tool outcomes: " + join_strings(projection.tool_outcomes, " | "));
  }

  return join_strings(parts, " ");
}

SummaryProjection CompressionCoordinator::merge_with_existing(
    const std::optional<contracts::SummaryMemory>& existing_summary,
    SummaryProjection projection) const {
  if (!existing_summary.has_value()) {
    return projection;
  }

  if (existing_summary->summary_text.has_value() &&
      !existing_summary->summary_text->empty()) {
    if (projection.summary_text.empty()) {
      projection.summary_text = *existing_summary->summary_text;
    } else if (projection.summary_text != *existing_summary->summary_text) {
      projection.summary_text = *existing_summary->summary_text + "\nUpdate: " +
                                projection.summary_text;
    }
  }

  if (existing_summary->source_turn_ids.has_value()) {
    merge_unique(projection.source_turn_ids, *existing_summary->source_turn_ids);
  }
  if (existing_summary->decisions_made.has_value()) {
    merge_unique(projection.decisions_made, *existing_summary->decisions_made);
  }
  if (existing_summary->confirmed_facts.has_value()) {
    merge_unique(projection.confirmed_facts, *existing_summary->confirmed_facts);
  }
  if (existing_summary->tool_outcomes.has_value()) {
    merge_unique(projection.tool_outcomes, *existing_summary->tool_outcomes);
  }

  projection.estimated_tokens = estimate_projection_tokens(projection);
  return projection;
}

contracts::SummaryMemory CompressionCoordinator::materialize_summary(
    const CompressionInput& input,
    const SummaryProjection& projection) const {
  contracts::SummaryMemory summary;
  summary.summary_id = build_summary_id(input);
  summary.session_id = input.session_id;
  summary.summary_text = projection.summary_text;
  if (!projection.source_turn_ids.empty()) {
    summary.source_turn_ids = projection.source_turn_ids;
  }
  if (!projection.decisions_made.empty()) {
    summary.decisions_made = projection.decisions_made;
  }
  if (!projection.confirmed_facts.empty()) {
    summary.confirmed_facts = projection.confirmed_facts;
  }
  if (!projection.tool_outcomes.empty()) {
    summary.tool_outcomes = projection.tool_outcomes;
  }
  summary.created_at = current_time_ms();
  summary.tags = std::vector<std::string>{"compression"};
  return summary;
}

}  // namespace dasall::memory
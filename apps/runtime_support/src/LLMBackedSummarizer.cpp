#include "LLMBackedSummarizer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "ILLMManager.h"
#include "LLMGenerateRequest.h"

namespace dasall::apps::runtime_support {
namespace {

constexpr std::string_view kSummaryStage = "response";
constexpr std::string_view kSummaryTaskType = "summary";
constexpr std::string_view kSummaryResponseFormat = "json_object";
constexpr std::string_view kSummaryUsedWarning = "llm_summary_used";

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string join_values(const std::vector<std::string>& values,
                                      std::string_view separator) {
  std::ostringstream stream;
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index != 0U) {
      stream << separator;
    }
    stream << values[index];
  }
  return stream.str();
}

[[nodiscard]] std::vector<std::string> collect_source_turn_ids(
    const std::vector<dasall::contracts::Turn>& turns) {
  std::vector<std::string> turn_ids;
  for (const auto& turn : turns) {
    if (!turn.turn_id.has_value() || turn.turn_id->empty()) {
      continue;
    }
    if (std::find(turn_ids.begin(), turn_ids.end(), *turn.turn_id) ==
        turn_ids.end()) {
      turn_ids.push_back(*turn.turn_id);
    }
  }
  return turn_ids;
}

void append_optional_line(std::ostringstream& stream,
                          std::string_view label,
                          const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty()) {
    return;
  }
  stream << label << *value << '\n';
}

void append_optional_list(std::ostringstream& stream,
                          std::string_view label,
                          const std::optional<std::vector<std::string>>& values) {
  if (!values.has_value() || values->empty()) {
    return;
  }
  stream << label << join_values(*values, ", ") << '\n';
}

[[nodiscard]] std::string build_session_summary(
    const memory::SummaryGenerationRequest& request) {
  std::ostringstream stream;
  stream << "session_id: " << request.session_id << '\n';

  if (request.existing_summary.has_value()) {
    append_optional_line(stream, "existing_summary_text: ",
                         request.existing_summary->summary_text);
    append_optional_list(stream, "existing_decisions: ",
                         request.existing_summary->decisions_made);
    append_optional_list(stream, "existing_confirmed_facts: ",
                         request.existing_summary->confirmed_facts);
    append_optional_list(stream, "existing_tool_outcomes: ",
                         request.existing_summary->tool_outcomes);
  }

  for (std::size_t index = 0U; index < request.source_turns.size(); ++index) {
    const auto& turn = request.source_turns[index];
    stream << "turn[" << index << "]\n";
    append_optional_line(stream, "id: ", turn.turn_id);
    append_optional_line(stream, "user_input: ", turn.user_input);
    append_optional_line(stream, "agent_response: ", turn.agent_response);
    append_optional_list(stream, "tool_call_refs: ", turn.tool_call_refs);
    append_optional_list(stream, "observation_refs: ", turn.observation_refs);
  }

  return stream.str();
}

[[nodiscard]] std::string build_user_goal(
    const memory::SummaryGenerationRequest& request) {
  std::ostringstream stream;
  stream << "请为 memory session 生成结构化摘要，保留关键决策、确认事实与工具结果。";
  if (!request.session_id.empty()) {
    stream << " session_id=" << request.session_id << '.';
  }
  return stream.str();
}

[[nodiscard]] std::string build_constraints(
    const memory::SummaryGenerationRequest& request,
    const LLMBackedSummarizer::Options& options) {
  std::ostringstream stream;
  stream << "summary_text 必须非空；decisions_made/confirmed_facts/tool_outcomes 缺失时返回空数组；"
            "只允许基于 source_turns 与 existing_summary 已出现的信息，不得编造；"
            "输出使用简体中文。";
  if (request.target_token_budget > 0) {
    stream << " target_token_budget=" << request.target_token_budget << ';';
  }
  if (!request.strategy_hint.empty()) {
    stream << " strategy_hint=" << request.strategy_hint << ';';
  }
  if (!options.profile_id.empty()) {
    stream << " profile_id=" << options.profile_id << ';';
  }
  return stream.str();
}

[[nodiscard]] std::string make_request_id(
    const memory::SummaryGenerationRequest& request,
    std::int64_t now_ms) {
  return "memory-summary-" + request.session_id + "-" + std::to_string(now_ms);
}

[[nodiscard]] std::uint32_t resolve_max_output_tokens(
    const memory::SummaryGenerationRequest& request,
    const LLMBackedSummarizer::Options& options) {
  if (request.target_token_budget > 0) {
    return static_cast<std::uint32_t>(
        std::clamp(request.target_token_budget, 64, 1024));
  }

  return std::max<std::uint32_t>(options.default_max_output_tokens, 64U);
}

[[nodiscard]] std::optional<std::size_t> find_json_value_start(
    std::string_view payload,
    std::string_view key,
    std::size_t offset = 0U) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const std::size_t key_position = payload.find(needle, offset);
  if (key_position == std::string_view::npos) {
    return std::nullopt;
  }

  const std::size_t colon_position = payload.find(':', key_position + needle.size());
  if (colon_position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t value_position = colon_position + 1U;
  while (value_position < payload.size() &&
         std::isspace(static_cast<unsigned char>(payload[value_position])) != 0) {
    ++value_position;
  }

  if (value_position >= payload.size()) {
    return std::nullopt;
  }

  return value_position;
}

[[nodiscard]] std::optional<std::string> extract_json_string_field(
    std::string_view payload,
    std::string_view key) {
  const auto value_start = find_json_value_start(payload, key);
  if (!value_start.has_value() || payload[*value_start] != '"') {
    return std::nullopt;
  }

  std::string value;
  bool escaping = false;
  for (std::size_t index = *value_start + 1U; index < payload.size(); ++index) {
    const char character = payload[index];
    if (escaping) {
      switch (character) {
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        case '\\':
        case '"':
        case '/':
          value.push_back(character);
          break;
        default:
          value.push_back(character);
          break;
      }
      escaping = false;
      continue;
    }

    if (character == '\\') {
      escaping = true;
      continue;
    }

    if (character == '"') {
      return value;
    }

    value.push_back(character);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>> extract_json_string_array_field(
    std::string_view payload,
    std::string_view key) {
  const auto value_start = find_json_value_start(payload, key);
  if (!value_start.has_value() || payload[*value_start] != '[') {
    return std::nullopt;
  }

  std::vector<std::string> values;
  std::string current;
  bool in_string = false;
  bool escaping = false;

  for (std::size_t index = *value_start + 1U; index < payload.size(); ++index) {
    const char character = payload[index];
    if (in_string) {
      if (escaping) {
        switch (character) {
          case 'n':
            current.push_back('\n');
            break;
          case 'r':
            current.push_back('\r');
            break;
          case 't':
            current.push_back('\t');
            break;
          case '\\':
          case '"':
          case '/':
            current.push_back(character);
            break;
          default:
            current.push_back(character);
            break;
        }
        escaping = false;
        continue;
      }

      if (character == '\\') {
        escaping = true;
        continue;
      }

      if (character == '"') {
        values.push_back(current);
        current.clear();
        in_string = false;
        continue;
      }

      current.push_back(character);
      continue;
    }

    if (character == ']') {
      return values;
    }

    if (std::isspace(static_cast<unsigned char>(character)) != 0 || character == ',') {
      continue;
    }

    if (character != '"') {
      return std::nullopt;
    }

    in_string = true;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<memory::SummaryProjection> parse_projection(
    std::string_view payload,
    const memory::SummaryGenerationRequest& request) {
  const auto summary_text = extract_json_string_field(payload, "summary_text");
  if (!summary_text.has_value() || summary_text->empty()) {
    return std::nullopt;
  }

  memory::SummaryProjection projection;
  projection.summary_text = *summary_text;
  if (const auto decisions =
          extract_json_string_array_field(payload, "decisions_made");
      decisions.has_value()) {
    projection.decisions_made = *decisions;
  }
  if (const auto facts = extract_json_string_array_field(payload, "confirmed_facts");
      facts.has_value()) {
    projection.confirmed_facts = *facts;
  }
  if (const auto outcomes =
          extract_json_string_array_field(payload, "tool_outcomes");
      outcomes.has_value()) {
    projection.tool_outcomes = *outcomes;
  }
  projection.source_turn_ids = collect_source_turn_ids(request.source_turns);
  return projection;
}

[[nodiscard]] memory::SummaryGenerationResult make_fallback_result(
    std::string warning,
    const std::optional<dasall::contracts::ResultCode>& result_code = std::nullopt) {
  memory::SummaryGenerationResult result;
  if (!warning.empty()) {
    result.warnings.push_back(std::move(warning));
  }
  result.fallback_used = true;
  result.degraded = true;
  result.result_code = result_code;
  return result;
}

}  // namespace

LLMBackedSummarizer::LLMBackedSummarizer(
  std::shared_ptr<llm::ILLMManager> llm_manager)
  : LLMBackedSummarizer(std::move(llm_manager), Options{}) {}

LLMBackedSummarizer::LLMBackedSummarizer(
  std::shared_ptr<llm::ILLMManager> llm_manager,
  Options options)
    : llm_manager_(std::move(llm_manager)), options_(std::move(options)) {}

memory::SummaryGenerationResult LLMBackedSummarizer::summarize(
    const memory::SummaryGenerationRequest& request) {
  if (llm_manager_ == nullptr) {
    return make_fallback_result("llm_summary_manager_missing");
  }

  const std::int64_t now_ms = current_time_ms();
  const std::string request_id = make_request_id(request, now_ms);

  contracts::LLMRequest llm_request;
  llm_request.request_id = request_id;
  llm_request.llm_call_id = request_id + ".call";
  llm_request.model_route = options_.model_route;
  llm_request.request_mode = contracts::LLMRequestMode::Unary;
  llm_request.messages = std::vector<std::string>{
      std::string("summarize memory session ") + request.session_id,
  };
  llm_request.created_at = now_ms;
  llm_request.output_schema_ref = options_.output_schema_ref;
  llm_request.response_format = std::string(kSummaryResponseFormat);
  llm_request.max_output_tokens = resolve_max_output_tokens(request, options_);
  llm_request.timeout_ms = options_.timeout_ms;
  llm_request.tags = std::vector<std::string>{
      std::string("user_goal=") + build_user_goal(request),
      std::string("constraints=") + build_constraints(request, options_),
      std::string("session_summary=") + build_session_summary(request),
      std::string("profile_id=") + options_.profile_id,
      std::string("owner_module=runtime_support"),
  };

  llm::LLMGenerateRequest generate_request{
      .stage = std::string(kSummaryStage),
      .task_type = std::string(kSummaryTaskType),
      .request = std::move(llm_request),
      .prompt_release_id_override = options_.prompt_release_id.empty()
                                        ? std::nullopt
                                        : std::make_optional(options_.prompt_release_id),
      .selection_hint = nullptr,
  };

  const auto llm_result = llm_manager_->generate(generate_request);
  if (!llm_result.has_consistent_values()) {
    return make_fallback_result("llm_summary_inconsistent_result",
                                llm_result.code);
  }

  if (!llm_result.response.has_value() ||
      !llm_result.response->content_payload.has_value() ||
      llm_result.response->content_payload->empty()) {
    return make_fallback_result("llm_summary_missing_payload",
                                llm_result.code);
  }

  const auto projection = parse_projection(*llm_result.response->content_payload,
                                           request);
  if (!projection.has_value()) {
    return make_fallback_result("llm_summary_parse_failed", llm_result.code);
  }

  memory::SummaryGenerationResult result;
  result.projection = *projection;
  result.warnings.push_back(std::string(kSummaryUsedWarning));
  return result;
}

}  // namespace dasall::apps::runtime_support
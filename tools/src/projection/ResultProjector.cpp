#include "projection/ResultProjector.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ResultCode.h"
#include "observation/Observation.h"
#include "observation/ObservationDigest.h"
#include "observation/ObservationSource.h"

namespace {

using dasall::contracts::Observation;
using dasall::contracts::ObservationDigest;
using dasall::contracts::ObservationSource;
using dasall::contracts::ToolResult;
using dasall::tools::ToolInvocationContext;
using dasall::tools::ToolInvocationEnvelope;
using dasall::tools::ToolRouteFacts;
using dasall::tools::route::ToolRouteDecision;

constexpr std::size_t kMaxSummaryChars = 512U;
constexpr std::size_t kMaxFactChars = 256U;
constexpr std::size_t kMaxProjectedFacts = 6U;
constexpr std::size_t kMaxProjectedArrayItems = 5U;
constexpr std::size_t kMaxPayloadProjectionBytes = 4096U;
constexpr std::string_view kTruncatedMarker = " [truncated]";

enum class JsonValueKind {
  String,
  Scalar,
  Object,
  Array,
  Unknown,
};

struct JsonField {
  std::string key;
  std::string raw_value;
  JsonValueKind kind = JsonValueKind::Unknown;
};

struct ProjectionStats {
  bool summary_truncated = false;
  bool payload_truncated = false;
  std::size_t truncated_fact_count = 0U;
  std::size_t payload_bytes = 0U;
  std::size_t projected_bytes = 0U;
  std::size_t total_fields = 0U;
  std::size_t projected_fields = 0U;
};

[[nodiscard]] bool is_whitespace(char value) {
  return std::isspace(static_cast<unsigned char>(value)) != 0;
}

[[nodiscard]] std::string_view trim_view(std::string_view value) {
  while (!value.empty() && is_whitespace(value.front())) {
    value.remove_prefix(1);
  }
  while (!value.empty() && is_whitespace(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] bool is_json_string_literal(std::string_view value) {
  const auto trimmed = trim_view(value);
  return trimmed.size() >= 2U && trimmed.front() == '"' && trimmed.back() == '"';
}

[[nodiscard]] bool looks_like_json_object(std::string_view value) {
  const auto trimmed = trim_view(value);
  return trimmed.size() >= 2U && trimmed.front() == '{' && trimmed.back() == '}';
}

[[nodiscard]] bool looks_like_json_array(std::string_view value) {
  const auto trimmed = trim_view(value);
  return trimmed.size() >= 2U && trimmed.front() == '[' && trimmed.back() == ']';
}

[[nodiscard]] std::string route_kind_string(dasall::contracts::ToolIRRoute route) {
  switch (route) {
    case dasall::contracts::ToolIRRoute::LocalTool:
      return "builtin";
    case dasall::contracts::ToolIRRoute::WorkflowEngine:
      return "workflow";
    case dasall::contracts::ToolIRRoute::MCPRemote:
      return "mcp";
    case dasall::contracts::ToolIRRoute::Unspecified:
      break;
  }

  return "unspecified";
}

[[nodiscard]] std::string build_observation_id(const ToolResult& result) {
  return std::string("observation:") +
         result.tool_call_id.value_or(std::string("unknown_call"));
}

[[nodiscard]] ToolRouteFacts build_route_facts(const ToolRouteDecision& route_decision) {
  ToolRouteFacts facts;
  facts.route_kind = route_kind_string(route_decision.route);
  facts.route_ref = route_decision.server_id.has_value()
                        ? route_decision.server_id
                        : std::optional<std::string>(route_decision.lane_key);
  facts.decision_reason = route_decision.reason_code;
  facts.plugin_id = std::nullopt;
  facts.server_id = route_decision.server_id;
  return facts;
}

[[nodiscard]] std::vector<std::string> build_citations(
    const ToolResult& result,
    const ToolRouteDecision& route_decision) {
  std::vector<std::string> citations;
  if (result.tool_call_id.has_value()) {
    citations.push_back("tool_call:" + *result.tool_call_id);
  }
  citations.push_back("route_kind:" + route_kind_string(route_decision.route));
  if (!route_decision.reason_code.empty()) {
    citations.push_back("route_reason:" + route_decision.reason_code);
  }
  if (route_decision.server_id.has_value()) {
    citations.push_back("server:" + *route_decision.server_id);
  }
  return citations;
}

[[nodiscard]] std::string derive_failure_reason(const ToolResult& result) {
  if (result.error.has_value() && !result.error->details.message.empty()) {
    return result.error->details.message;
  }

  return "tool.manager.execution_failed";
}

[[nodiscard]] std::string truncate_with_marker(
    std::string_view value,
    std::size_t max_chars,
    bool* truncated) {
  if (value.size() <= max_chars) {
    if (truncated != nullptr) {
      *truncated = false;
    }
    return std::string(value);
  }

  if (truncated != nullptr) {
    *truncated = true;
  }
  if (max_chars <= kTruncatedMarker.size()) {
    return std::string(value.substr(0, max_chars));
  }

  return std::string(value.substr(0, max_chars - kTruncatedMarker.size())) +
         std::string(kTruncatedMarker);
}

[[nodiscard]] std::string unescape_json_string(std::string_view value) {
  auto trimmed = trim_view(value);
  if (trimmed.size() >= 2U && trimmed.front() == '"' && trimmed.back() == '"') {
    trimmed.remove_prefix(1);
    trimmed.remove_suffix(1);
  }

  std::string output;
  output.reserve(trimmed.size());
  bool escaped = false;
  for (const char current : trimmed) {
    if (escaped) {
      switch (current) {
        case '"':
        case '\\':
        case '/':
          output.push_back(current);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        default:
          output.push_back(current);
          break;
      }
      escaped = false;
      continue;
    }

    if (current == '\\') {
      escaped = true;
      continue;
    }

    output.push_back(current);
  }

  return output;
}

[[nodiscard]] std::vector<std::string_view> split_top_level_entries(std::string_view value) {
  std::vector<std::string_view> entries;
  const auto trimmed = trim_view(value);
  if (trimmed.empty()) {
    return entries;
  }

  std::size_t start = 0U;
  int object_depth = 0;
  int array_depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 0; index < trimmed.size(); ++index) {
    const char current = trimmed[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        in_string = false;
      }
      continue;
    }

    if (current == '"') {
      in_string = true;
      continue;
    }
    if (current == '{') {
      ++object_depth;
      continue;
    }
    if (current == '}') {
      --object_depth;
      continue;
    }
    if (current == '[') {
      ++array_depth;
      continue;
    }
    if (current == ']') {
      --array_depth;
      continue;
    }
    if (current == ',' && object_depth == 0 && array_depth == 0) {
      entries.push_back(trim_view(trimmed.substr(start, index - start)));
      start = index + 1U;
    }
  }

  if (start <= trimmed.size()) {
    entries.push_back(trim_view(trimmed.substr(start)));
  }
  entries.erase(
      std::remove_if(
          entries.begin(),
          entries.end(),
          [](std::string_view item) { return trim_view(item).empty(); }),
      entries.end());
  return entries;
}

[[nodiscard]] std::size_t find_top_level_colon(std::string_view value) {
  int object_depth = 0;
  int array_depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 0; index < value.size(); ++index) {
    const char current = value[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        in_string = false;
      }
      continue;
    }

    if (current == '"') {
      in_string = true;
      continue;
    }
    if (current == '{') {
      ++object_depth;
      continue;
    }
    if (current == '}') {
      --object_depth;
      continue;
    }
    if (current == '[') {
      ++array_depth;
      continue;
    }
    if (current == ']') {
      --array_depth;
      continue;
    }
    if (current == ':' && object_depth == 0 && array_depth == 0) {
      return index;
    }
  }

  return std::string_view::npos;
}

[[nodiscard]] JsonValueKind detect_value_kind(std::string_view value) {
  const auto trimmed = trim_view(value);
  if (trimmed.empty()) {
    return JsonValueKind::Unknown;
  }
  if (looks_like_json_object(trimmed)) {
    return JsonValueKind::Object;
  }
  if (looks_like_json_array(trimmed)) {
    return JsonValueKind::Array;
  }
  if (is_json_string_literal(trimmed)) {
    return JsonValueKind::String;
  }
  return JsonValueKind::Scalar;
}

[[nodiscard]] std::vector<JsonField> parse_top_level_object(std::string_view payload) {
  std::vector<JsonField> fields;
  auto trimmed = trim_view(payload);
  if (!looks_like_json_object(trimmed)) {
    return fields;
  }

  trimmed.remove_prefix(1);
  trimmed.remove_suffix(1);
  for (const auto entry : split_top_level_entries(trimmed)) {
    const auto delimiter = find_top_level_colon(entry);
    if (delimiter == std::string_view::npos) {
      continue;
    }

    const auto key_view = trim_view(entry.substr(0, delimiter));
    const auto value_view = trim_view(entry.substr(delimiter + 1U));
    if (!is_json_string_literal(key_view)) {
      continue;
    }

    fields.push_back(JsonField{
        .key = unescape_json_string(key_view),
        .raw_value = std::string(value_view),
        .kind = detect_value_kind(value_view),
    });
  }

  return fields;
}

[[nodiscard]] bool is_summary_source_key(std::string_view key) {
  return key == "summary" || key == "message" || key == "description" ||
         key == "result";
}

[[nodiscard]] std::string format_array_preview(std::string_view raw_value) {
  auto trimmed = trim_view(raw_value);
  if (!looks_like_json_array(trimmed)) {
    return std::string(trimmed);
  }

  trimmed.remove_prefix(1);
  trimmed.remove_suffix(1);
  const auto entries = split_top_level_entries(trimmed);
  std::string preview = "[";
  const auto projected = std::min(entries.size(), kMaxProjectedArrayItems);
  for (std::size_t index = 0; index < projected; ++index) {
    if (index > 0U) {
      preview += ", ";
    }

    const auto item = trim_view(entries[index]);
    if (looks_like_json_object(item)) {
      preview += "{...}";
    } else if (looks_like_json_array(item)) {
      preview += "[...]";
    } else if (is_json_string_literal(item)) {
      preview += unescape_json_string(item);
    } else {
      preview += std::string(item);
    }
  }
  preview += "]";
  if (projected < entries.size()) {
    preview += " [" + std::to_string(projected) + " of " +
               std::to_string(entries.size()) + " total]";
  }
  return preview;
}

[[nodiscard]] std::string format_fact(
    const JsonField& field,
    ProjectionStats& stats) {
  std::string text = field.key + "=";
  switch (field.kind) {
    case JsonValueKind::String:
      text += unescape_json_string(field.raw_value);
      break;
    case JsonValueKind::Scalar:
      text += std::string(trim_view(field.raw_value));
      break;
    case JsonValueKind::Object:
      text += "{...}";
      break;
    case JsonValueKind::Array:
      text += format_array_preview(field.raw_value);
      break;
    case JsonValueKind::Unknown:
      text += std::string(trim_view(field.raw_value));
      break;
  }

  bool truncated = false;
  const auto formatted = truncate_with_marker(text, kMaxFactChars, &truncated);
  if (truncated) {
    ++stats.truncated_fact_count;
  }
  return formatted;
}

[[nodiscard]] std::string build_success_summary(
    const ToolResult& result,
    const ToolRouteDecision& route_decision,
    ProjectionStats& stats) {
  const auto payload = result.payload.value_or(std::string{});
  if (!payload.empty()) {
    const auto fields = parse_top_level_object(payload);
    for (const auto& field : fields) {
      if (!is_summary_source_key(field.key)) {
        continue;
      }

      std::string candidate;
      if (field.kind == JsonValueKind::String) {
        candidate = unescape_json_string(field.raw_value);
      } else if (field.kind == JsonValueKind::Scalar) {
        candidate = std::string(trim_view(field.raw_value));
      }
      if (!candidate.empty()) {
        return truncate_with_marker(candidate, kMaxSummaryChars, &stats.summary_truncated);
      }
    }

    if (!looks_like_json_object(payload) && !looks_like_json_array(payload)) {
      return truncate_with_marker(payload, kMaxSummaryChars, &stats.summary_truncated);
    }
  }

  const auto fallback = std::string("Tool ") +
                        result.tool_name.value_or(std::string("unknown_tool")) +
                        " completed via " + route_kind_string(route_decision.route);
  return truncate_with_marker(fallback, kMaxSummaryChars, &stats.summary_truncated);
}

[[nodiscard]] std::string build_failure_summary(
    const ToolResult& result,
    ProjectionStats& stats) {
  if (result.error.has_value()) {
    const auto category = result.error->failure_type.has_value()
                              ? std::string(dasall::contracts::result_code_category_name(
                                    *result.error->failure_type))
                              : std::string("unknown");
    const auto message = !result.error->details.message.empty()
                             ? result.error->details.message
                             : std::string("tool.manager.execution_failed");
    return truncate_with_marker(message + " (" + category + ")",
                                kMaxSummaryChars,
                                &stats.summary_truncated);
  }

  return truncate_with_marker("tool.manager.execution_failed",
                              kMaxSummaryChars,
                              &stats.summary_truncated);
}

[[nodiscard]] std::vector<std::string> build_success_facts(
    const ToolResult& result,
    const ToolRouteDecision& route_decision,
    ProjectionStats& stats) {
  std::vector<std::string> facts{
      std::string("status=success"),
      std::string("route=") + route_kind_string(route_decision.route),
      std::string("decision=") + route_decision.reason_code,
  };

  const auto payload = result.payload.value_or(std::string{});
  if (payload.empty()) {
    return facts;
  }

  if (looks_like_json_object(payload)) {
    const auto parsed_fields = parse_top_level_object(payload);
    std::vector<JsonField> projected_fields;
    projected_fields.reserve(parsed_fields.size());
    for (const auto& field : parsed_fields) {
      if (!is_summary_source_key(field.key)) {
        projected_fields.push_back(field);
      }
    }

    stats.total_fields = projected_fields.size();
    const auto fact_budget = kMaxProjectedFacts > facts.size() ? kMaxProjectedFacts - facts.size() : 0U;
    const auto projected_count = std::min(projected_fields.size(), fact_budget);
    stats.projected_fields = projected_count;
    for (std::size_t index = 0; index < projected_count; ++index) {
      facts.push_back(format_fact(projected_fields[index], stats));
    }
    return facts;
  }

  if (looks_like_json_array(payload)) {
    stats.total_fields = 1U;
    stats.projected_fields = 1U;
    facts.push_back(format_fact(JsonField{
                       .key = std::string("items"),
                       .raw_value = payload,
                       .kind = JsonValueKind::Array,
                   },
                   stats));
    return facts;
  }

  facts.push_back(std::string("payload_bytes=") + std::to_string(payload.size()));
  return facts;
}

[[nodiscard]] std::vector<std::string> build_failure_facts(
    const ToolResult& result,
    const ToolRouteDecision& route_decision) {
  std::vector<std::string> facts{
      std::string("status=failure"),
      std::string("route=") + route_kind_string(route_decision.route),
      std::string("decision=") + route_decision.reason_code,
  };

  if (result.error.has_value()) {
    if (result.error->failure_type.has_value()) {
      facts.push_back(
          std::string("failure_type=") +
          std::string(dasall::contracts::result_code_category_name(*result.error->failure_type)));
    }
    if (!result.error->details.stage.empty()) {
      facts.push_back(std::string("error_stage=") + result.error->details.stage);
    }
  }

  return facts;
}

[[nodiscard]] std::optional<std::vector<std::string>> build_omitted_details(
    const ToolResult& result,
    const ProjectionStats& stats) {
  std::vector<std::string> omitted;
  if (stats.payload_truncated) {
    const auto omitted_ratio = stats.payload_bytes == 0U
                                   ? 0U
                                   : ((stats.payload_bytes - std::min(stats.projected_bytes, stats.payload_bytes)) * 100U) /
                                         stats.payload_bytes;
    omitted.push_back(
        std::string("payload_bytes=") + std::to_string(stats.payload_bytes) +
        ", projected_bytes=" + std::to_string(stats.projected_bytes) +
        ", omitted_ratio=" + std::to_string(omitted_ratio) + "%");
  }
  if (stats.total_fields > stats.projected_fields) {
    omitted.push_back(
        std::string("total_fields=") + std::to_string(stats.total_fields) +
        ", projected_fields=" + std::to_string(stats.projected_fields));
  }
  if (!result.success.value_or(!result.error.has_value()) && result.payload.has_value() &&
      !result.payload->empty()) {
    omitted.push_back("failure payload suppressed");
  }
  if (omitted.empty()) {
    return std::nullopt;
  }
  return omitted;
}

[[nodiscard]] float build_confidence(const ProjectionStats& stats) {
  float confidence = 1.0f;
  if (stats.summary_truncated) {
    confidence -= 0.1f;
  }
  confidence -= 0.05f * static_cast<float>(stats.truncated_fact_count);
  if (stats.payload_truncated) {
    confidence -= 0.15f;
  }
  return std::clamp(confidence, 0.1f, 1.0f);
}

}  // namespace

namespace dasall::tools::projection {

ToolInvocationEnvelope ResultProjector::project(
    const contracts::ToolResult& result,
    const route::ToolRouteDecision& route_decision,
    const ToolInvocationContext& invocation_context) const {
  if (result.success.value_or(!result.error.has_value())) {
    return project_success(result, route_decision, invocation_context);
  }
  return project_failure(result, route_decision, invocation_context);
}

ToolInvocationEnvelope ResultProjector::project_success(
    const contracts::ToolResult& result,
    const route::ToolRouteDecision& route_decision,
    const ToolInvocationContext& invocation_context) const {
  static_cast<void>(invocation_context);
  ToolInvocationEnvelope envelope;
  envelope.tool_result = result;
  envelope.observation = build_observation(result);
  envelope.observation_digest = build_digest(result, route_decision, invocation_context);
  envelope.route_facts = build_route_facts(route_decision);
  envelope.evidence_refs = build_citations(result, route_decision);
  envelope.failure_reason_code = std::nullopt;
  return envelope;
}

ToolInvocationEnvelope ResultProjector::project_failure(
    const contracts::ToolResult& result,
    const route::ToolRouteDecision& route_decision,
    const ToolInvocationContext& invocation_context) const {
  static_cast<void>(invocation_context);
  ToolInvocationEnvelope envelope;
  envelope.tool_result = result;
  envelope.observation = build_observation(result);
  envelope.observation_digest = build_digest(result, route_decision, invocation_context);
  envelope.route_facts = build_route_facts(route_decision);
  envelope.evidence_refs = build_citations(result, route_decision);
  envelope.failure_reason_code = derive_failure_reason(result);
  return envelope;
}

Observation ResultProjector::build_observation(const contracts::ToolResult& result) const {
  return Observation{
      .observation_id = build_observation_id(result),
      .source = ObservationSource::ToolExecution,
      .success = result.success,
      .payload = result.payload,
      .created_at = result.completed_at,
      .error = result.error,
      .side_effects = result.side_effects,
      .tool_call_id = result.tool_call_id,
      .worker_task_id = result.worker_task_id,
      .request_id = result.request_id,
      .goal_id = result.goal_id,
      .duration_ms = result.duration_ms,
      .tags = result.tags,
  };
}

ObservationDigest ResultProjector::build_digest(
    const contracts::ToolResult& result,
    const route::ToolRouteDecision& route_decision,
    const ToolInvocationContext& invocation_context) const {
  static_cast<void>(invocation_context);

  ProjectionStats stats;
  stats.payload_bytes = result.payload.has_value() ? result.payload->size() : 0U;
  stats.payload_truncated = stats.payload_bytes > kMaxPayloadProjectionBytes;

  std::string summary;
  std::vector<std::string> key_facts;
  if (result.success.value_or(!result.error.has_value())) {
    summary = build_success_summary(result, route_decision, stats);
    key_facts = build_success_facts(result, route_decision, stats);
  } else {
    summary = build_failure_summary(result, stats);
    key_facts = build_failure_facts(result, route_decision);
  }

  stats.projected_bytes = summary.size();
  for (const auto& fact : key_facts) {
    stats.projected_bytes += fact.size();
  }

  return ObservationDigest{
      .observation_id = build_observation_id(result),
      .summary = std::move(summary),
      .key_facts = std::move(key_facts),
      .citations = build_citations(result, route_decision),
      .confidence = build_confidence(stats),
      .omitted_details = build_omitted_details(result, stats),
      .source = ObservationSource::ToolExecution,
      .created_at = result.completed_at,
      .tags = result.tags,
  };
}

}  // namespace dasall::tools::projection
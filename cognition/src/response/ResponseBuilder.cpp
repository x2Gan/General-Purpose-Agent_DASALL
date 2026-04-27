#include "IResponseBuilder.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "validation/InputBoundaryValidator.h"

namespace dasall::cognition {
namespace {

enum class ResponseMode {
  ObservationProjection,
  TemplateFallback,
  Unavailable,
};

struct RedactionOutcome {
  std::string value;
  std::vector<std::string> omitted_details;
  bool redacted = false;
};

struct ResponseEnvelope {
  std::string response_mode;
  std::string summary_text;
  std::vector<std::string> structured_sections;
  std::vector<std::string> omitted_details;
  bool fallback_used = false;
};

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] bool has_non_empty_value(const std::optional<std::string>& value) {
  return value.has_value() && !value->empty();
}

[[nodiscard]] std::string escape_json_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }

  return escaped;
}

void append_json_array(std::string& json,
                       const std::string_view& key,
                       const std::vector<std::string>& values) {
  json += "\"" + std::string(key) + "\":[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      json += ',';
    }
    json += "\"" + escape_json_string(values[index]) + "\"";
  }
  json += ']';
}

[[nodiscard]] std::string build_structured_payload(const ResponseEnvelope& envelope) {
  std::string payload;
  payload.reserve(256U + envelope.summary_text.size());
  payload += "{";
  payload += "\"response_mode\":\"" + escape_json_string(envelope.response_mode) + "\",";
  payload += "\"summary_text\":\"" + escape_json_string(envelope.summary_text) + "\",";
  append_json_array(payload, "structured_sections", envelope.structured_sections);
  payload += ',';
  append_json_array(payload, "omitted_details", envelope.omitted_details);
  payload += ",\"fallback_used\":";
  payload += envelope.fallback_used ? "true" : "false";
  payload += '}';
  return payload;
}

[[nodiscard]] contracts::AgentResult make_agent_result(const ResponseBuildRequest& request,
                                                       contracts::AgentResultStatus status,
                                                       std::string response_text) {
  contracts::AgentResult result;
  result.result_id = request.request_id + "-cognition-response";
  result.status = status;
  result.result_code = 0;
  result.response_text = std::move(response_text);
  result.task_completed = (status == contracts::AgentResultStatus::Completed);
  result.created_at = current_time_ms();
  result.request_id = request.request_id;
  result.trace_id = request.trace_id;
  result.goal_id = request.goal_contract.goal_id;
  result.tags = std::vector<std::string>{"cognition", "response_builder"};
  return result;
}

[[nodiscard]] contracts::ErrorInfo make_error_info(contracts::ResultCode result_code,
                                                   std::string stage,
                                                   std::string message,
                                                   std::string source_ref) {
  return contracts::ErrorInfo{
      .failure_type = contracts::classify_result_code(result_code),
      .retryable = false,
      .safe_to_replan = false,
      .details = contracts::ErrorDetails{
          .code = static_cast<int>(result_code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = contracts::ErrorSourceRefMinimal{
          .ref_type = "cognition.component",
          .ref_id = std::move(source_ref),
      },
  };
}

[[nodiscard]] ResponseBuildResult build_error_result(contracts::ResultCode result_code,
                                                     std::string stage,
                                                     std::string message,
                                                     std::string diagnostic) {
  ResponseBuildResult result;
  result.result_code = result_code;
  result.error_info = make_error_info(result_code, stage, message, diagnostic);
  result.diagnostics.push_back(std::move(diagnostic));
  return result;
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

[[nodiscard]] bool replace_json_assignment(std::string& text,
                                           const std::string_view& key,
                                           std::vector<std::string>& omitted_details) {
  const std::string pattern = "\"" + std::string(key) + "\":\"";
  bool replaced = false;
  std::size_t position = 0U;
  while ((position = text.find(pattern, position)) != std::string::npos) {
    const auto value_begin = position + pattern.size();
    const auto value_end = text.find('"', value_begin);
    if (value_end == std::string::npos) {
      break;
    }
    text.replace(value_begin, value_end - value_begin, "[REDACTED]");
    append_unique(omitted_details, std::string("redacted:") + std::string(key));
    position = value_begin + std::string_view{"[REDACTED]"}.size();
    replaced = true;
  }
  return replaced;
}

[[nodiscard]] bool replace_plain_assignment(std::string& text,
                                            const std::string_view& key,
                                            std::vector<std::string>& omitted_details) {
  const std::string pattern = std::string(key) + "=";
  bool replaced = false;
  std::size_t position = 0U;
  while ((position = text.find(pattern, position)) != std::string::npos) {
    const auto value_begin = position + pattern.size();
    const auto value_end = text.find_first_of(",; \n\r\t}", value_begin);
    const auto effective_end = value_end == std::string::npos ? text.size() : value_end;
    text.replace(value_begin, effective_end - value_begin, "[REDACTED]");
    append_unique(omitted_details, std::string("redacted:") + std::string(key));
    position = value_begin + std::string_view{"[REDACTED]"}.size();
    replaced = true;
  }
  return replaced;
}

[[nodiscard]] RedactionOutcome redact_unsafe_fields(const CognitionConfig& config,
                                                    std::string text) {
  RedactionOutcome outcome{
      .value = std::move(text),
      .omitted_details = {},
      .redacted = false,
  };
  if (!config.observability.redact_context_payload) {
    return outcome;
  }

  static constexpr std::string_view kSensitiveKeys[] = {
      "reasoning_content",
      "raw_prompt",
      "prompt_bundle",
      "provider_payload",
      "reasoning_trace",
      "api_token",
      "authorization",
      "secret_key",
  };

  for (const auto& key : kSensitiveKeys) {
    const auto json_replaced = replace_json_assignment(outcome.value, key, outcome.omitted_details);
    const auto plain_replaced = replace_plain_assignment(outcome.value, key, outcome.omitted_details);
    outcome.redacted = outcome.redacted || json_replaced || plain_replaced;
  }

  return outcome;
}

[[nodiscard]] bool template_fallback_enabled(const CognitionConfig& config,
                                             const ResponseBuildRequest& request) {
  return config.response.template_fallback_enabled && request.build_hints.allow_template_fallback;
}

[[nodiscard]] std::optional<std::string> observation_payload(const ResponseBuildRequest& request) {
  if (!request.latest_observation.has_value() || !has_non_empty_value(request.latest_observation->payload)) {
    return std::nullopt;
  }

  return request.latest_observation->payload;
}

[[nodiscard]] ResponseMode select_response_mode(const CognitionConfig& config,
                                                const ResponseBuildRequest& request) {
  const auto template_allowed = template_fallback_enabled(config, request);
  if (request.build_hints.prefer_template && template_allowed) {
    return ResponseMode::TemplateFallback;
  }

  if (observation_payload(request).has_value()) {
    return ResponseMode::ObservationProjection;
  }

  if (template_allowed) {
    return ResponseMode::TemplateFallback;
  }

  return ResponseMode::Unavailable;
}

[[nodiscard]] std::string clamp_output_size(const ResponseBuildRequest& request,
                                            std::string text,
                                            std::vector<std::string>& diagnostics,
                                            std::vector<std::string>& omitted_details) {
  if (request.build_hints.max_summary_chars == 0U ||
      text.size() <= static_cast<std::size_t>(request.build_hints.max_summary_chars)) {
    return text;
  }

  const auto limit = static_cast<std::size_t>(request.build_hints.max_summary_chars);
  std::string clamped = text.substr(0U, limit);
  if (limit >= 3U) {
    clamped.replace(limit - 3U, 3U, "...");
  }
  diagnostics.push_back("response_clamped");
  append_unique(omitted_details, "clamped:max_summary_chars");
  return clamped;
}

[[nodiscard]] std::string derive_template_summary(const ResponseBuildRequest& request) {
  if (request.terminal_decision.has_value() && request.terminal_decision->response_outline.has_value() &&
      !request.terminal_decision->response_outline->summary.empty()) {
    return request.terminal_decision->response_outline->summary;
  }

  if (observation_payload(request).has_value()) {
    return *observation_payload(request);
  }

  if (has_non_empty_value(request.context_packet.current_goal_summary)) {
    return *request.context_packet.current_goal_summary;
  }

  if (has_non_empty_value(request.goal_contract.goal_description)) {
    return *request.goal_contract.goal_description;
  }

  return {};
}

[[nodiscard]] ResponseBuildResult build_with_llm(const CognitionConfig& config,
                                                 const ResponseBuildRequest& request) {
  const auto payload = observation_payload(request);
  if (!payload.has_value()) {
    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.llm_projection",
                              "response projection requires an observation payload",
                              "response_observation_payload_missing");
  }

  std::vector<std::string> diagnostics = {"response_mode:llm_projection"};
  auto redaction = redact_unsafe_fields(
      config, std::string("runtime unary integration completed: ") + *payload);
  if (redaction.redacted) {
    diagnostics.push_back("response_redacted");
  }

  ResponseEnvelope envelope{
      .response_mode = "llm_projection",
      .summary_text = redaction.value,
      .structured_sections = request.build_hints.required_sections,
      .omitted_details = redaction.omitted_details,
      .fallback_used = false,
  };
  envelope.summary_text =
      clamp_output_size(request, std::move(envelope.summary_text), diagnostics, envelope.omitted_details);

  auto agent_result = make_agent_result(request, contracts::AgentResultStatus::Completed,
                                        envelope.summary_text);
  agent_result.structured_payload = build_structured_payload(envelope);
  if (agent_result.tags.has_value()) {
    agent_result.tags->push_back("response_mode:llm_projection");
    if (redaction.redacted) {
      agent_result.tags->push_back("response_redacted");
    }
  }

  ResponseBuildResult result;
  result.agent_result = std::move(agent_result);
  result.diagnostics = std::move(diagnostics);
  return result;
}

[[nodiscard]] ResponseBuildResult build_with_template(const CognitionConfig& config,
                                                      const ResponseBuildRequest& request) {
  auto summary = derive_template_summary(request);
  if (summary.empty()) {
    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.template_fallback",
                              "response template fallback requires a terminal summary seed",
                              "response_template_seed_missing");
  }

  std::vector<std::string> diagnostics = {"response_mode:template_fallback",
                                          "response_template_fallback"};
  auto redaction = redact_unsafe_fields(config, std::move(summary));
  if (redaction.redacted) {
    diagnostics.push_back("response_redacted");
  }

  ResponseEnvelope envelope{
      .response_mode = "template_fallback",
      .summary_text = std::move(redaction.value),
      .structured_sections = request.build_hints.required_sections,
      .omitted_details = redaction.omitted_details,
      .fallback_used = true,
  };
  envelope.summary_text =
      clamp_output_size(request, std::move(envelope.summary_text), diagnostics, envelope.omitted_details);

  auto agent_result = make_agent_result(
      request, contracts::AgentResultStatus::PartiallyCompleted, envelope.summary_text);
  agent_result.structured_payload = build_structured_payload(envelope);
  if (agent_result.tags.has_value()) {
    agent_result.tags->push_back("response_mode:template_fallback");
    agent_result.tags->push_back("response_fallback_used");
    if (redaction.redacted) {
      agent_result.tags->push_back("response_redacted");
    }
  }

  ResponseBuildResult result;
  result.agent_result = std::move(agent_result);
  result.fallback_used = true;
  result.diagnostics = std::move(diagnostics);
  return result;
}

void apply_invalid_response_result(
    ResponseBuildResult& result,
    const validation::InputBoundaryValidationResult& validation_result) {
  result.result_code = contracts::ResultCode::ValidationFieldMissing;
  result.error_info = validation_result.error_info;
  result.diagnostics.push_back("invalid_input");
}

class ResponseBuilder final : public IResponseBuilder {
 public:
  explicit ResponseBuilder(CognitionConfig config) : config_(std::move(config)) {}

  [[nodiscard]] ResponseBuildResult build(
      const ResponseBuildRequest& request) override {
    ResponseBuildResult result;
    const auto validation_result =
        validation::InputBoundaryValidator::validate_response_request(request);
    if (!validation_result.ok()) {
      apply_invalid_response_result(result, validation_result);
      return result;
    }

    switch (select_response_mode(config_, request)) {
      case ResponseMode::ObservationProjection:
        return build_with_llm(config_, request);
      case ResponseMode::TemplateFallback:
        return build_with_template(config_, request);
      case ResponseMode::Unavailable:
        return build_error_result(contracts::ResultCode::PolicyDenied,
                                  "cognition.response.mode_selection",
                                  "response builder has no observation payload and template fallback is disabled",
                                  "response_mode_unavailable");
    }

    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.mode_selection",
                              "response builder failed to resolve a terminal output mode",
                              "response_mode_resolution_failed");
  }

 private:
  CognitionConfig config_;
};

}  // namespace

std::unique_ptr<IResponseBuilder> create_response_builder(const CognitionConfig& config) {
  return std::make_unique<ResponseBuilder>(config);
}

}  // namespace dasall::cognition
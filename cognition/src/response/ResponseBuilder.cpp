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

#include "llm/CognitionLlmBridge.h"
#include "validation/InputBoundaryValidator.h"

namespace dasall::cognition {
namespace {

enum class ResponseMode {
  LlmBridge,
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

using llm_bridge::CognitionLlmBridge;
using llm_bridge::StageLlmCallResult;

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
                                                const ResponseBuildRequest& request,
                                                bool bridge_available) {
  const auto template_allowed = template_fallback_enabled(config, request);
  if (request.build_hints.prefer_template && template_allowed) {
    return ResponseMode::TemplateFallback;
  }

  if (observation_payload(request).has_value() && bridge_available) {
    return ResponseMode::LlmBridge;
  }

  if (observation_payload(request).has_value()) {
    return ResponseMode::ObservationProjection;
  }

  if (template_allowed) {
    return ResponseMode::TemplateFallback;
  }

  return ResponseMode::Unavailable;
}

[[nodiscard]] StageModelHint make_response_model_hint(const ResponseBuildRequest& request) {
  return StageModelHint{
      .stage_name = "response",
      .task_type = "final_response",
      .capability_tier = ModelCapabilityTier::Standard,
      .max_output_tokens = request.build_hints.max_summary_chars,
      .deadline_ms = 2500U,
      .requires_structured_output = false,
      .requires_reasoning_trace = false,
      .cost_sensitivity = 0.0F,
      .preferred_provider = {},
  };
}

[[nodiscard]] std::vector<std::string> make_response_messages(
    const ResponseBuildRequest& request,
    const std::string& payload) {
  std::vector<std::string> messages;
  messages.push_back("stage=response; task_type=final_response");
  if (request.goal_contract.goal_description.has_value()) {
    messages.push_back("goal=" + *request.goal_contract.goal_description);
  }
  if (request.context_packet.user_turn.has_value()) {
    messages.push_back("user_turn=" + *request.context_packet.user_turn);
  }
  if (request.context_packet.current_goal_summary.has_value()) {
    messages.push_back("goal_summary=" + *request.context_packet.current_goal_summary);
  }
  if (request.terminal_decision.has_value() &&
      request.terminal_decision->response_outline.has_value()) {
    messages.push_back("terminal_summary=" +
                       request.terminal_decision->response_outline->summary);
  }
  messages.push_back("observation_payload=" + payload);
  return messages;
}

[[nodiscard]] llm_bridge::StageLlmCallRequest make_response_stage_call_request(
    const ResponseBuildRequest& request,
    const std::string& payload) {
  llm_bridge::StageLlmCallRequest bridge_request;
  bridge_request.request_id = request.request_id;
  bridge_request.trace_id = request.trace_id;
  bridge_request.llm_call_id = request.request_id + ":response:final_response";
  bridge_request.stage_name = "response";
  bridge_request.task_type = "final_response";
  bridge_request.messages = make_response_messages(request, payload);
  bridge_request.model_hint = make_response_model_hint(request);
  bridge_request.schema_spec = llm_bridge::StageSchemaSpec{
      .schema_kind = llm_bridge::StageSchemaKind::Text,
      .output_schema_ref = {},
      .allow_plain_text_fallback = true,
  };
  return bridge_request;
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

[[nodiscard]] ResponseBuildResult build_with_observation_projection(
    const CognitionConfig& config,
    const ResponseBuildRequest& request) {
  const auto payload = observation_payload(request);
  if (!payload.has_value()) {
    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.observation_projection",
                              "response projection requires an observation payload",
                              "response_observation_payload_missing");
  }

  std::vector<std::string> diagnostics = {"response_mode:observation_projection",
                                          "llm_bridge.unavailable:response"};
  auto redaction = redact_unsafe_fields(
      config, std::string("runtime unary integration completed: ") + *payload);
  if (redaction.redacted) {
    diagnostics.push_back("response_redacted");
  }

  ResponseEnvelope envelope{
      .response_mode = "observation_projection",
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
    agent_result.tags->push_back("response_mode:observation_projection");
    if (redaction.redacted) {
      agent_result.tags->push_back("response_redacted");
    }
  }

  ResponseBuildResult result;
  result.agent_result = std::move(agent_result);
  result.diagnostics = std::move(diagnostics);
  return result;
}

void append_bridge_diagnostics(std::vector<std::string>& diagnostics,
                               const StageLlmCallResult& bridge_result) {
  append_unique(diagnostics, "llm_bridge.invoked:response");
  if (bridge_result.error_info.has_value()) {
    append_unique(diagnostics, "llm_bridge.failed:response");
  } else {
    append_unique(diagnostics, "llm_bridge.completed:response");
  }
  for (const auto& diagnostic : bridge_result.diagnostics) {
    append_unique(diagnostics, diagnostic);
  }
  for (const auto& warning : bridge_result.warnings) {
    append_unique(diagnostics, std::string{"llm_bridge.warning:"} + warning);
  }
}

[[nodiscard]] ResponseBuildResult build_bridge_error_result(
    const StageLlmCallResult& bridge_result,
    std::string diagnostic) {
  ResponseBuildResult result;
  result.result_code =
      bridge_result.result_code.value_or(contracts::ResultCode::RuntimeRetryExhausted);
  result.error_info = bridge_result.error_info;
  result.diagnostics = bridge_result.diagnostics;
  result.diagnostics.push_back(std::move(diagnostic));
  return result;
}

[[nodiscard]] ResponseBuildResult build_with_llm_bridge(
    const CognitionConfig& config,
    const ResponseBuildRequest& request,
    const CognitionLlmBridge& bridge);

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

[[nodiscard]] ResponseBuildResult build_with_llm_bridge(
    const CognitionConfig& config,
    const ResponseBuildRequest& request,
    const CognitionLlmBridge& bridge) {
  const auto payload = observation_payload(request);
  if (!payload.has_value()) {
    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.llm_bridge",
                              "response bridge requires an observation payload",
                              "response_observation_payload_missing");
  }

  const auto bridge_result =
      bridge.invoke_stage(make_response_stage_call_request(request, *payload));
  if (bridge_result.error_info.has_value()) {
    if (template_fallback_enabled(config, request)) {
      auto fallback = build_with_template(config, request);
      fallback.diagnostics.push_back("response_llm_bridge_failed");
      append_bridge_diagnostics(fallback.diagnostics, bridge_result);
      return fallback;
    }

    return build_bridge_error_result(bridge_result, "response_llm_bridge_failed");
  }

  if (!bridge_result.response.has_value() ||
      !bridge_result.response->content_payload.has_value() ||
      bridge_result.response->content_payload->empty()) {
    if (template_fallback_enabled(config, request)) {
      auto fallback = build_with_template(config, request);
      fallback.diagnostics.push_back("response_llm_bridge_empty_payload");
      append_bridge_diagnostics(fallback.diagnostics, bridge_result);
      return fallback;
    }

    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.llm_bridge",
                              "response bridge returned no content payload",
                              "response_llm_bridge_empty_payload");
  }

  std::vector<std::string> diagnostics = {"response_mode:llm_bridge"};
  append_bridge_diagnostics(diagnostics, bridge_result);

  auto redaction = redact_unsafe_fields(config, *bridge_result.response->content_payload);
  if (redaction.redacted) {
    diagnostics.push_back("response_redacted");
  }
  for (const auto& warning : bridge_result.warnings) {
    append_unique(redaction.omitted_details, std::string{"bridge_warning:"} + warning);
  }

  ResponseEnvelope envelope{
      .response_mode = "llm_bridge",
      .summary_text = std::move(redaction.value),
      .structured_sections = request.build_hints.required_sections,
      .omitted_details = std::move(redaction.omitted_details),
      .fallback_used = bridge_result.fallback_used,
  };
  envelope.summary_text =
      clamp_output_size(request, std::move(envelope.summary_text), diagnostics, envelope.omitted_details);

  auto agent_result = make_agent_result(request, contracts::AgentResultStatus::Completed,
                                        envelope.summary_text);
  agent_result.structured_payload = build_structured_payload(envelope);
  if (agent_result.tags.has_value()) {
    agent_result.tags->push_back("response_mode:llm_bridge");
    agent_result.tags->push_back(std::string{"llm_route:"} + bridge_result.resolved_route);
    if (redaction.redacted) {
      agent_result.tags->push_back("response_redacted");
    }
  }

  ResponseBuildResult result;
  result.agent_result = std::move(agent_result);
  result.fallback_used = bridge_result.fallback_used;
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
  explicit ResponseBuilder(CognitionConfig config,
                           CognitionRuntimeDependencies dependencies = {})
      : config_(std::move(config)),
        llm_bridge_(dependencies.llm_manager != nullptr
                        ? std::make_shared<CognitionLlmBridge>(
                              std::move(dependencies.llm_manager))
                        : nullptr) {}

  [[nodiscard]] ResponseBuildResult build(
      const ResponseBuildRequest& request) override {
    ResponseBuildResult result;
    const auto validation_result =
        validation::InputBoundaryValidator::validate_response_request(request);
    if (!validation_result.ok()) {
      apply_invalid_response_result(result, validation_result);
      return result;
    }

    switch (select_response_mode(config_, request, llm_bridge_ != nullptr)) {
      case ResponseMode::LlmBridge:
        return build_with_llm_bridge(config_, request, *llm_bridge_);
      case ResponseMode::ObservationProjection:
        return build_with_observation_projection(config_, request);
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
  std::shared_ptr<CognitionLlmBridge> llm_bridge_;
};

}  // namespace

std::unique_ptr<IResponseBuilder> create_response_builder(const CognitionConfig& config) {
  return std::make_unique<ResponseBuilder>(config, CognitionRuntimeDependencies{});
}

std::unique_ptr<IResponseBuilder> create_response_builder(
    const CognitionConfig& config,
    CognitionRuntimeDependencies dependencies) {
  return std::make_unique<ResponseBuilder>(config, std::move(dependencies));
}

}  // namespace dasall::cognition

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

#include "StagePolicyResolver.h"
#include "RuntimePolicySnapshot.h"
#include "config/CognitionConfigProjector.h"
#include "error/ResultCode.h"
#include "llm/CognitionLlmBridge.h"
#include "observability/CognitionTelemetry.h"
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
using observability::CognitionTelemetry;
using observability::DegradeTelemetryRecord;
using observability::StageTelemetryContext;
using observability::TelemetryEmitResult;
using observability::TelemetryField;

[[nodiscard]] const StageModelHint* find_stage_model_hint(
    const policy::StageExecutionPlan& plan,
    std::string_view stage_name,
    std::string_view task_type) {
  for (const auto& hint : plan.stage_model_hints) {
    if (hint.stage_name == stage_name && hint.task_type == task_type) {
      return &hint;
    }
  }

  return nullptr;
}

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] std::uint32_t elapsed_ms_since(
    const std::chrono::steady_clock::time_point& started_at) {
  return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started_at)
                                    .count());
}

void ignore_emit_result(const TelemetryEmitResult&) {}

void append_detail_field(std::vector<TelemetryField>& fields,
                         std::string key,
                         std::string value) {
  if (value.empty()) {
    return;
  }

  fields.push_back(TelemetryField{
      .key = std::move(key),
      .value = std::move(value),
  });
}

void append_detail_field(std::vector<TelemetryField>& fields,
                         std::string key,
                         const bool value) {
  append_detail_field(fields,
                      std::move(key),
                      std::string(value ? "true" : "false"));
}

void emit_response_checkpoint(CognitionTelemetry& telemetry,
                              const StageTelemetryContext& context,
                              std::string step,
                              std::string outcome,
                              std::vector<TelemetryField> extra_fields = {}) {
  std::vector<TelemetryField> fields;
  fields.reserve(extra_fields.size() + 3U);
  append_detail_field(fields, "pipeline", "response");
  append_detail_field(fields, "step", std::move(step));
  append_detail_field(fields, "outcome", std::move(outcome));
  for (auto& field : extra_fields) {
    append_detail_field(fields, std::move(field.key), std::move(field.value));
  }

  ignore_emit_result(
      telemetry.emit_detail_event("pipeline.checkpoint", context, std::move(fields)));
}

[[nodiscard]] std::string replay_bool_value(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string escape_replay_value(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

void append_replay_line(std::string& serialized,
                        std::string_view key,
                        std::string_view value) {
  serialized += key;
  serialized += '=';
  serialized += escape_replay_value(value);
  serialized += '\n';
}

template <typename Number>
void append_replay_number_line(std::string& serialized,
                               std::string_view key,
                               Number value) {
  append_replay_line(serialized, key, std::to_string(value));
}

void append_replay_optional_line(std::string& serialized,
                                 std::string_view key,
                                 const std::optional<std::string>& value) {
  if (value.has_value()) {
    append_replay_line(serialized, key, *value);
  }
}

void append_replay_sorted_values(std::string& serialized,
                                 std::string_view prefix,
                                 const std::vector<std::string>& values) {
  auto sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  append_replay_number_line(serialized,
                            std::string(prefix) + "_count",
                            sorted_values.size());
  for (std::size_t index = 0; index < sorted_values.size(); ++index) {
    append_replay_line(serialized,
                       std::string(prefix) + "." + std::to_string(index),
                       sorted_values[index]);
  }
}

[[nodiscard]] std::string replay_action_decision_kind_name(
    const decision::ActionDecisionKind kind) {
  switch (kind) {
    case decision::ActionDecisionKind::NoDecision:
      return "NoDecision";
    case decision::ActionDecisionKind::AskClarification:
      return "AskClarification";
    case decision::ActionDecisionKind::ExecuteAction:
      return "ExecuteAction";
    case decision::ActionDecisionKind::DirectResponse:
      return "DirectResponse";
    case decision::ActionDecisionKind::ConvergeSafe:
      return "ConvergeSafe";
  }

  return "Unknown";
}

[[nodiscard]] std::string replay_agent_status_name(
    const contracts::AgentResultStatus status) {
  switch (status) {
    case contracts::AgentResultStatus::Unspecified:
      return "Unspecified";
    case contracts::AgentResultStatus::Completed:
      return "Completed";
    case contracts::AgentResultStatus::Failed:
      return "Failed";
    case contracts::AgentResultStatus::PartiallyCompleted:
      return "PartiallyCompleted";
    case contracts::AgentResultStatus::Cancelled:
      return "Cancelled";
    case contracts::AgentResultStatus::Timeout:
      return "Timeout";
  }

  return "Unknown";
}

[[nodiscard]] std::string serialize_build_request(const ResponseBuildRequest& request) {
  std::string serialized;
  append_replay_line(serialized, "surface", "build.request");
  append_replay_line(serialized, "caller_domain", request.caller_domain);
  append_replay_line(serialized, "request_id", request.request_id);
  append_replay_line(serialized, "trace_id", request.trace_id);
  append_replay_line(serialized, "profile_id", request.profile_id);
  append_replay_optional_line(serialized, "goal_id", request.goal_contract.goal_id);
  append_replay_optional_line(serialized,
                              "goal_description",
                              request.goal_contract.goal_description);
  append_replay_optional_line(serialized, "user_turn", request.context_packet.user_turn);
  append_replay_line(serialized,
                     "latest_observation_present",
                     replay_bool_value(request.latest_observation.has_value()));
  if (request.latest_observation.has_value()) {
    append_replay_optional_line(serialized,
                                "latest_observation_payload",
                                request.latest_observation->payload);
  }
  if (request.terminal_decision.has_value()) {
    append_replay_line(serialized,
                       "terminal_decision_kind",
                       replay_action_decision_kind_name(
                           request.terminal_decision->decision_kind));
  }
  append_replay_line(serialized,
                     "prefer_template",
                     replay_bool_value(request.build_hints.prefer_template));
  append_replay_line(serialized,
                     "prefer_observation_projection",
                     replay_bool_value(
                         request.build_hints.prefer_observation_projection));
  append_replay_line(serialized,
                     "allow_template_fallback",
                     replay_bool_value(
                         request.build_hints.allow_template_fallback));
  append_replay_number_line(serialized,
                            "required_section_count",
                            request.build_hints.required_sections.size());
  append_replay_number_line(serialized,
                            "max_summary_chars",
                            request.build_hints.max_summary_chars);
  return serialized;
}

[[nodiscard]] std::string serialize_build_result(const ResponseBuildResult& result) {
  std::string serialized;
  append_replay_line(serialized, "surface", "build.result");
  if (result.result_code.has_value()) {
    append_replay_number_line(serialized,
                              "result_code",
                              static_cast<int>(*result.result_code));
  }
  append_replay_line(serialized,
                     "fallback_used",
                     replay_bool_value(result.fallback_used));
  if (result.agent_result.has_value()) {
    append_replay_line(serialized,
                       "agent_status",
                       replay_agent_status_name(
                           result.agent_result->status.value_or(
                               contracts::AgentResultStatus::Unspecified)));
    if (result.agent_result->result_code.has_value()) {
      append_replay_number_line(serialized,
                                "agent_result_code",
                                *result.agent_result->result_code);
    }
    append_replay_line(serialized,
                       "task_completed",
                       replay_bool_value(
                           result.agent_result->task_completed.value_or(false)));
    append_replay_line(serialized,
                       "has_structured_payload",
                       replay_bool_value(
                           result.agent_result->structured_payload.has_value()));
  }
  append_replay_sorted_values(serialized, "diagnostic", result.diagnostics);
  return serialized;
}

void emit_replay_trace(CognitionTelemetry& telemetry,
                       const StageTelemetryContext& context,
                       std::string event_name,
                       std::string serialized_value) {
  std::vector<TelemetryField> fields;
  fields.push_back(TelemetryField{
      .key = "serialized_value",
      .value = std::move(serialized_value),
  });
  ignore_emit_result(
      telemetry.emit_detail_event(std::move(event_name), context, std::move(fields)));
}

[[nodiscard]] std::string response_mode_name(const ResponseMode mode) {
  switch (mode) {
    case ResponseMode::LlmBridge:
      return "llm_bridge";
    case ResponseMode::ObservationProjection:
      return "observation_projection";
    case ResponseMode::TemplateFallback:
      return "template_fallback";
    case ResponseMode::Unavailable:
      return "unavailable";
  }

  return "unavailable";
}

[[nodiscard]] std::string resolved_response_mode(const ResponseBuildResult& result,
                                                const ResponseMode selected_mode) {
  for (const auto& diagnostic : result.diagnostics) {
    static constexpr std::string_view kPrefix = "response_mode:";
    if (diagnostic.rfind(kPrefix, 0) == 0) {
      return diagnostic.substr(kPrefix.size());
    }
  }

  return response_mode_name(selected_mode);
}

[[nodiscard]] bool contains_diagnostic(const std::vector<std::string>& diagnostics,
                                       const std::string_view expected) {
  return std::find(diagnostics.begin(), diagnostics.end(), expected) !=
         diagnostics.end();
}

[[nodiscard]] std::optional<std::string> find_prefixed_diagnostic_value(
    const std::vector<std::string>& diagnostics,
    const std::string_view prefix) {
  for (const auto& diagnostic : diagnostics) {
    if (diagnostic.rfind(prefix, 0) == 0 && diagnostic.size() > prefix.size()) {
      return diagnostic.substr(prefix.size());
    }
  }

  return std::nullopt;
}

void append_error_type_field(std::vector<TelemetryField>& fields,
                             const std::optional<contracts::ErrorInfo>& error_info,
                             const std::optional<contracts::ResultCode>& result_code) {
  if (error_info.has_value() && error_info->failure_type.has_value()) {
    append_detail_field(
        fields,
        "error_type",
        std::string(contracts::result_code_category_name(*error_info->failure_type)));
    return;
  }

  if (result_code.has_value()) {
    append_detail_field(fields,
                        "error_type",
                        std::string(contracts::result_code_category_name(
                            contracts::classify_result_code(*result_code))));
  }
}

[[nodiscard]] std::string resolved_response_source(const ResponseBuildResult& result,
                                                   const ResponseMode selected_mode) {
  if (contains_diagnostic(result.diagnostics, "llm_bridge.invoked:response")) {
    return "llm_bridge";
  }

  const auto mode = resolved_response_mode(result, selected_mode);
  if (mode == "unavailable") {
    return "mode_selection";
  }

  return mode;
}

[[nodiscard]] std::string require_goal_id(const contracts::GoalContract& goal_contract) {
  return goal_contract.goal_id.value_or(std::string{});
}

[[nodiscard]] std::string derive_model_hint_tier(const ResponseBuildRequest& request) {
  if (request.build_hints.prefer_template) {
    return "economy";
  }
  if (request.build_hints.prefer_observation_projection) {
    return "balanced";
  }
  return "standard";
}

[[nodiscard]] StageTelemetryContext make_response_stage_context(
    const ResponseBuildRequest& request,
    bool fallback_used,
    std::optional<contracts::ResultCode> result_code = std::nullopt) {
  return StageTelemetryContext{
      .request_id = request.request_id,
      .goal_id = require_goal_id(request.goal_contract),
      .profile_id = request.profile_id,
      .stage = "response",
      .trace_id = request.trace_id,
      .model_hint_tier = derive_model_hint_tier(request),
      .fallback_used = fallback_used,
      .result_code = result_code.has_value()
                         ? std::optional<int>(static_cast<int>(*result_code))
                         : std::nullopt,
      .structured_projection = {},
  };
}

[[nodiscard]] std::string derive_degrade_reason(
    const std::vector<std::string>& diagnostics) {
  if (std::find(diagnostics.begin(), diagnostics.end(), "response_llm_bridge_failed") !=
      diagnostics.end()) {
    return "llm_bridge_failed";
  }
  if (std::find(diagnostics.begin(), diagnostics.end(), "response_llm_bridge_empty_payload") !=
      diagnostics.end()) {
    return "llm_bridge_empty_payload";
  }
  if (std::find(diagnostics.begin(), diagnostics.end(), "response_llm_route_fallback") !=
      diagnostics.end()) {
    return "llm_route_fallback";
  }
  return "template_fallback";
}

[[nodiscard]] std::string derive_fallback_mode(
    const std::vector<std::string>& diagnostics) {
  if (std::find(diagnostics.begin(), diagnostics.end(), "response_llm_route_fallback") !=
      diagnostics.end()) {
    return "llm_route_fallback";
  }

  return "template_fallback";
}

[[nodiscard]] DegradeTelemetryRecord make_degrade_record(
    const ResponseBuildResult& result) {
  DegradeTelemetryRecord record{
      .fallback_mode = derive_fallback_mode(result.diagnostics),
      .reason = derive_degrade_reason(result.diagnostics),
    .resolved_route = find_prefixed_diagnostic_value(result.diagnostics, "route:"),
    .failure_category =
      find_prefixed_diagnostic_value(result.diagnostics, "llm_failure:"),
    .error_type = find_prefixed_diagnostic_value(result.diagnostics, "error_type:"),
      .payload_excerpt = std::nullopt,
      .omitted_details = result.diagnostics,
      .audit_refs = {},
  };
  if (result.agent_result.has_value() && result.agent_result->response_text.has_value()) {
    record.payload_excerpt = *result.agent_result->response_text;
  }
  return record;
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

[[nodiscard]] bool template_fallback_enabled(
    const CognitionConfig& config,
    const ResponseBuildRequest& request,
    const policy::StageExecutionPlan* response_plan = nullptr) {
  const auto enabled_in_config = config.response.template_fallback_enabled;
  const auto enabled_in_plan = response_plan == nullptr || response_plan->template_fallback_enabled;
  return enabled_in_config && enabled_in_plan && request.build_hints.allow_template_fallback;
}

[[nodiscard]] std::optional<std::string> observation_payload(const ResponseBuildRequest& request) {
  if (!request.latest_observation.has_value() || !has_non_empty_value(request.latest_observation->payload)) {
    return std::nullopt;
  }

  return request.latest_observation->payload;
}

[[nodiscard]] ResponseMode select_response_mode(const CognitionConfig& config,
                                                const ResponseBuildRequest& request,
                                                bool bridge_available,
                                                const policy::StageExecutionPlan* response_plan = nullptr) {
  const auto template_allowed = template_fallback_enabled(config, request, response_plan);
  if (response_plan != nullptr &&
      response_plan->fallback_mode == policy::StageFallbackMode::TemplatePreferred &&
      template_allowed) {
    return ResponseMode::TemplateFallback;
  }

  if (request.build_hints.prefer_template && template_allowed) {
    return ResponseMode::TemplateFallback;
  }

  if (observation_payload(request).has_value() &&
      request.build_hints.prefer_observation_projection) {
    return ResponseMode::ObservationProjection;
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

[[nodiscard]] StageModelHint make_response_model_hint(const ResponseBuildRequest& request,
                                                      const StageModelHint* stage_model_hint) {
  if (stage_model_hint != nullptr) {
    return *stage_model_hint;
  }

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
  const std::string& payload,
  const StageModelHint* stage_model_hint) {
  llm_bridge::StageLlmCallRequest bridge_request;
  bridge_request.request_id = request.request_id;
  bridge_request.trace_id = request.trace_id;
  bridge_request.llm_call_id = request.request_id + ":response:final_response";
  bridge_request.stage_name = "response";
  bridge_request.task_type = "final_response";
  bridge_request.messages = make_response_messages(request, payload);
  bridge_request.model_hint = make_response_model_hint(request, stage_model_hint);
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
  if (bridge_result.fallback_used) {
    append_unique(diagnostics, "response_llm_route_fallback");
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
  const CognitionLlmBridge& bridge,
  const policy::StageExecutionPlan* response_plan = nullptr);

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
    const CognitionLlmBridge& bridge,
    const policy::StageExecutionPlan* response_plan) {
  const auto payload = observation_payload(request);
  if (!payload.has_value()) {
    return build_error_result(contracts::ResultCode::RuntimeRetryExhausted,
                              "cognition.response.llm_bridge",
                              "response bridge requires an observation payload",
                              "response_observation_payload_missing");
  }

  const auto* response_hint = response_plan != nullptr
                                  ? find_stage_model_hint(
                                        *response_plan, "response", "final_response")
                                  : nullptr;
  if (response_plan != nullptr && response_hint == nullptr) {
    return build_error_result(contracts::ResultCode::PolicyDenied,
                              "cognition.response.policy",
                              "runtime policy snapshot did not expose the response bridge hint",
                              "response_policy_hints_missing");
  }

  const auto bridge_result =
      bridge.invoke_stage(make_response_stage_call_request(request, *payload, response_hint));
  if (bridge_result.error_info.has_value()) {
    if (template_fallback_enabled(config, request, response_plan)) {
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
    if (template_fallback_enabled(config, request, response_plan)) {
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
  telemetry_(config_, observability::make_live_telemetry_sink(dependencies)),
        llm_bridge_(dependencies.llm_manager != nullptr
                        ? std::make_shared<CognitionLlmBridge>(
                              std::move(dependencies.llm_manager))
        : nullptr),
    policy_snapshot_(std::move(dependencies.policy_snapshot)) {}

  [[nodiscard]] ResponseBuildResult build(
      const ResponseBuildRequest& request) override {
    const auto started_at = std::chrono::steady_clock::now();
    auto telemetry_context = make_response_stage_context(request, false);
    ResponseBuildResult result;
    const auto validation_result =
        validation::InputBoundaryValidator::validate_response_request(request);
    if (!validation_result.ok()) {
      apply_invalid_response_result(result, validation_result);
      telemetry_context.result_code =
          static_cast<int>(contracts::ResultCode::ValidationFieldMissing);
      telemetry_context.latency_ms = elapsed_ms_since(started_at);
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

    emit_replay_trace(
      telemetry_, telemetry_context, "replay.trace.build.request", serialize_build_request(request));

    const auto response_plan = policy_snapshot_ != nullptr
                                   ? policy::StagePolicyResolver::resolve_response_plan(
                                         *policy_snapshot_, request)
                                   : std::optional<policy::StageExecutionPlan>{};
    if (policy_snapshot_ != nullptr && !response_plan.has_value()) {
      result = build_error_result(contracts::ResultCode::PolicyDenied,
                                  "cognition.response.policy",
                                  "runtime policy snapshot could not produce a response stage plan",
                                  "response_policy_projection_failed");
      telemetry_context = make_response_stage_context(request, false, result.result_code);
      telemetry_context.latency_ms = elapsed_ms_since(started_at);
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

      const auto* response_plan_ptr =
        response_plan.has_value() ? &(*response_plan) : nullptr;
      const auto fallback_allowed =
        template_fallback_enabled(config_, request, response_plan_ptr);
      const auto selected_mode =
        select_response_mode(config_, request, llm_bridge_ != nullptr, response_plan_ptr);

      emit_response_checkpoint(
        telemetry_,
        make_response_stage_context(request, false),
        "mode_selection",
        "selected",
        {
          TelemetryField{
            .key = "source",
            .value = policy_snapshot_ != nullptr ? "runtime_policy" : "config",
          },
          TelemetryField{
            .key = "mode",
            .value = response_mode_name(selected_mode),
          },
          TelemetryField{
            .key = "llm_bridge_enabled",
            .value = llm_bridge_ != nullptr ? "true" : "false",
          },
          TelemetryField{
            .key = "fallback_allowed",
            .value = fallback_allowed ? "true" : "false",
          },
        });

      switch (selected_mode) {
      case ResponseMode::LlmBridge:
        result = build_with_llm_bridge(
            config_,
            request,
            *llm_bridge_,
          response_plan_ptr);
        break;
      case ResponseMode::ObservationProjection:
        result = build_with_observation_projection(config_, request);
        break;
      case ResponseMode::TemplateFallback:
        result = build_with_template(config_, request);
        break;
      case ResponseMode::Unavailable:
        result = build_error_result(contracts::ResultCode::PolicyDenied,
                                    "cognition.response.mode_selection",
                                    "response builder has no observation payload and template fallback is disabled",
                                    "response_mode_unavailable");
        break;
    }

    telemetry_context = make_response_stage_context(request, result.fallback_used, result.result_code);
    telemetry_context.latency_ms = elapsed_ms_since(started_at);
    emit_replay_trace(
      telemetry_, telemetry_context, "replay.trace.build.result", serialize_build_result(result));
    const auto checkpoint_mode = resolved_response_mode(result, selected_mode);
    std::vector<TelemetryField> build_checkpoint_fields;
    build_checkpoint_fields.reserve(8U);
    build_checkpoint_fields.push_back(TelemetryField{
        .key = "mode",
        .value = checkpoint_mode,
    });
    build_checkpoint_fields.push_back(TelemetryField{
        .key = "source",
        .value = resolved_response_source(result, selected_mode),
    });
    build_checkpoint_fields.push_back(TelemetryField{
        .key = "diagnostic_count",
        .value = std::to_string(result.diagnostics.size()),
    });
    if (const auto resolved_route =
            find_prefixed_diagnostic_value(result.diagnostics, "route:");
        resolved_route.has_value()) {
      append_detail_field(
          build_checkpoint_fields, "resolved_route", resolved_route.value());
    }
    if (const auto failure_category =
            find_prefixed_diagnostic_value(result.diagnostics, "llm_failure:");
        failure_category.has_value()) {
      append_detail_field(
          build_checkpoint_fields, "failure_category", failure_category.value());
    }
    if (const auto error_type =
            find_prefixed_diagnostic_value(result.diagnostics, "error_type:");
        error_type.has_value()) {
      append_detail_field(build_checkpoint_fields, "error_type", error_type.value());
    } else {
      append_error_type_field(build_checkpoint_fields, result.error_info, result.result_code);
    }
    if (result.fallback_used) {
      append_detail_field(build_checkpoint_fields,
                          "fallback_mode",
                          derive_fallback_mode(result.diagnostics));
      append_detail_field(build_checkpoint_fields,
                          "degrade_reason",
                          derive_degrade_reason(result.diagnostics));
    }
    emit_response_checkpoint(
      telemetry_,
      telemetry_context,
      "build",
      result.error_info.has_value() ? "failed"
                      : (result.fallback_used ? "degraded" : "completed"),
      std::move(build_checkpoint_fields));
    if (result.error_info.has_value()) {
      ignore_emit_result(telemetry_.emit_stage_failed(telemetry_context, *result.error_info));
      return result;
    }

    if (result.fallback_used && result.agent_result.has_value()) {
      ignore_emit_result(telemetry_.emit_response_degraded(
          telemetry_context, make_degrade_record(result)));
    }

    return result;
  }

 private:
  CognitionConfig config_;
  CognitionTelemetry telemetry_;
  std::shared_ptr<CognitionLlmBridge> llm_bridge_;
  std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot_;
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

std::unique_ptr<IResponseBuilder> create_response_builder(
    const profiles::RuntimePolicySnapshot& snapshot,
    CognitionRuntimeDependencies dependencies) {
  const auto config = config::CognitionConfigProjector::project_config(snapshot);
  if (!config.has_value()) {
    return nullptr;
  }

  if (dependencies.policy_snapshot == nullptr) {
    dependencies.policy_snapshot =
        std::make_shared<const profiles::RuntimePolicySnapshot>(snapshot);
  }
  return std::make_unique<ResponseBuilder>(*config, std::move(dependencies));
}

}  // namespace dasall::cognition

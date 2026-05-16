#include "OpenAICompatibleAdapter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMBoundaryGuards.h"
#include "stream/IStreamObserver.h"

namespace {

using AdapterCallResult = dasall::llm::AdapterCallResult;
using AdapterProviderDiagnostics = dasall::llm::AdapterProviderDiagnostics;
using AdapterUsageFragment = dasall::llm::AdapterUsageFragment;
using HealthStatus = dasall::llm::HealthStatus;
using ILLMTransport = dasall::llm::ILLMTransport;
using LLMAdapterConfig = dasall::llm::LLMAdapterConfig;
using LLMTransportHeader = dasall::llm::LLMTransportHeader;
using LLMTransportMethod = dasall::llm::LLMTransportMethod;
using LLMTransportRequest = dasall::llm::LLMTransportRequest;
using LLMTransportResponse = dasall::llm::LLMTransportResponse;
using ResultCode = dasall::contracts::ResultCode;

constexpr std::string_view kOpenAICompatibleFamily = "openai_compatible";
constexpr std::string_view kGenerateStage = "llm.openai_compatible.generate";
constexpr std::string_view kStreamGenerateStage = "llm.openai_compatible.stream_generate";
constexpr std::string_view kHealthStage = "llm.openai_compatible.health_check";
constexpr std::string_view kChatCompletionsPath = "/chat/completions";
constexpr std::string_view kModelsPath = "/models";

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
  std::vector<std::string> sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

[[nodiscard]] bool config_has_required_values(const LLMAdapterConfig& config) {
  return config.adapter_family == kOpenAICompatibleFamily && !config.adapter_id.empty() &&
         !config.provider_instance_id.empty() && !config.base_url.empty() &&
         !config.base_url_alias.empty() && !config.auth_ref.empty() &&
         !config.snapshot_version.empty() && config.timeout_ms > 0U &&
         has_unique_values(config.header_refs);
}

[[nodiscard]] std::string trim_copy(std::string_view value) {
  std::size_t begin = 0U;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }

  std::size_t end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }

  return std::string(value.substr(begin, end - begin));
}

[[nodiscard]] std::string join_url(std::string_view base_url, std::string_view suffix) {
  if (base_url.empty()) {
    return std::string(suffix);
  }

  if (suffix.empty()) {
    return std::string(base_url);
  }

  const bool base_has_trailing_slash = base_url.back() == '/';
  const bool suffix_has_leading_slash = suffix.front() == '/';
  if (base_has_trailing_slash && suffix_has_leading_slash) {
    return std::string(base_url.substr(0U, base_url.size() - 1U)) + std::string(suffix);
  }

  if (!base_has_trailing_slash && !suffix_has_leading_slash) {
    return std::string(base_url) + "/" + std::string(suffix);
  }

  return std::string(base_url) + std::string(suffix);
}

[[nodiscard]] std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
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
        escaped.push_back(character);
        break;
    }
  }

  return escaped;
}

[[nodiscard]] std::pair<std::string, std::string> map_message(std::string_view raw_message) {
  static constexpr std::pair<std::string_view, std::string_view> kPrefixes[] = {
      {"system:", "system"},
      {"developer:", "developer"},
      {"assistant:", "assistant"},
      {"tool:", "tool"},
      {"user:", "user"},
  };

  for (const auto& [prefix, role] : kPrefixes) {
    if (raw_message.starts_with(prefix)) {
      const std::string content = trim_copy(raw_message.substr(prefix.size()));
      return {std::string(role), content.empty() ? std::string(raw_message) : content};
    }
  }

  return {"user", std::string(raw_message)};
}

[[nodiscard]] std::string build_messages_json(
    const std::vector<std::string>& messages) {
  std::string json = "[";
  for (std::size_t index = 0; index < messages.size(); ++index) {
    const auto [role, content] = map_message(messages[index]);
    if (index > 0U) {
      json += ",";
    }

    json += "{\"role\":\"" + escape_json(role) + "\",\"content\":\"" +
            escape_json(content) + "\"}";
  }
  json += "]";
  return json;
}

[[nodiscard]] std::optional<std::size_t> find_json_value_start(std::string_view payload,
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

[[nodiscard]] std::optional<std::string> parse_json_string(std::string_view payload,
                                                           std::size_t quote_position) {
  if (quote_position >= payload.size() || payload[quote_position] != '"') {
    return std::nullopt;
  }

  std::string value;
  bool escaping = false;
  for (std::size_t index = quote_position + 1U; index < payload.size(); ++index) {
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

[[nodiscard]] std::optional<std::string> extract_json_string_field(
    std::string_view payload,
    std::string_view key,
    std::size_t offset = 0U) {
  const auto value_start = find_json_value_start(payload, key, offset);
  if (!value_start.has_value() || payload[*value_start] != '"') {
    return std::nullopt;
  }

  return parse_json_string(payload, *value_start);
}

[[nodiscard]] std::optional<std::uint32_t> extract_json_uint_field(
    std::string_view payload,
    std::string_view key,
    std::size_t offset = 0U) {
  const auto value_start = find_json_value_start(payload, key, offset);
  if (!value_start.has_value()) {
    return std::nullopt;
  }

  std::size_t end = *value_start;
  while (end < payload.size() &&
         std::isdigit(static_cast<unsigned char>(payload[end])) != 0) {
    ++end;
  }

  if (end == *value_start) {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(
      std::stoul(std::string(payload.substr(*value_start, end - *value_start))));
}

[[nodiscard]] std::optional<std::string> extract_json_array_field(
    std::string_view payload,
    std::string_view key,
    std::size_t offset = 0U) {
  const auto value_start = find_json_value_start(payload, key, offset);
  if (!value_start.has_value() || payload[*value_start] != '[') {
    return std::nullopt;
  }

  bool in_string = false;
  bool escaping = false;
  int depth = 0;
  for (std::size_t index = *value_start; index < payload.size(); ++index) {
    const char character = payload[index];
    if (escaping) {
      escaping = false;
      continue;
    }

    if (character == '\\') {
      escaping = true;
      continue;
    }

    if (character == '"') {
      in_string = !in_string;
      continue;
    }

    if (in_string) {
      continue;
    }

    if (character == '[') {
      ++depth;
    } else if (character == ']') {
      --depth;
      if (depth == 0) {
        return std::string(payload.substr(*value_start, index - *value_start + 1U));
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::string build_tool_call_payload(std::string_view tool_calls_json) {
  const auto name = extract_json_string_field(tool_calls_json, "name");
  const auto arguments = extract_json_string_field(tool_calls_json, "arguments");

  std::string payload = "{\"tool_calls\":[{";
  if (name.has_value()) {
    payload += "\"name\":\"" + escape_json(*name) + "\"";
  }

  if (arguments.has_value()) {
    if (name.has_value()) {
      payload += ",";
    }
    payload += "\"arguments\":\"" + escape_json(*arguments) + "\"";
  }

  payload += "}]}";
  return payload;
}

[[nodiscard]] std::string resolve_transport_error_message(const LLMTransportResponse& response) {
  if (!response.error_message.empty()) {
    return response.error_message;
  }

  if (response.status_code == 0U) {
    return "transport did not return an HTTP status";
  }

  return "transport returned http status " + std::to_string(response.status_code);
}

[[nodiscard]] bool is_retryable_status(const LLMTransportResponse& response) {
  return response.status_code == 0U || response.status_code == 429U ||
         response.status_code >= 500U;
}

[[nodiscard]] std::vector<std::string> parse_sse_events(std::string_view payload) {
  std::vector<std::string> events;
  std::string current_data;
  std::size_t cursor = 0U;
  while (cursor <= payload.size()) {
    const auto line_end = payload.find('\n', cursor);
    auto line = line_end == std::string_view::npos
                    ? payload.substr(cursor)
                    : payload.substr(cursor, line_end - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    if (line.empty()) {
      if (!current_data.empty()) {
        events.push_back(current_data);
        current_data.clear();
      }
    } else if (line.rfind("data:", 0U) == 0U) {
      std::string data = trim_copy(line.substr(5U));
      if (!current_data.empty() && !data.empty()) {
        current_data.push_back('\n');
      }
      current_data += data;
    }

    if (line_end == std::string_view::npos) {
      break;
    }
    cursor = line_end + 1U;
  }

  if (!current_data.empty()) {
    events.push_back(current_data);
  }
  return events;
}

[[nodiscard]] std::string make_stream_session_id(const LLMAdapterConfig& config,
                                                 const dasall::contracts::LLMRequest& request) {
  if (request.llm_call_id.has_value() && !request.llm_call_id->empty()) {
    return config.adapter_id + ":" + *request.llm_call_id;
  }

  if (request.request_id.has_value() && !request.request_id->empty()) {
    return config.adapter_id + ":" + *request.request_id;
  }

  return config.adapter_id + ":stream";
}

}  // namespace

namespace dasall::llm {

OpenAICompatibleAdapter::OpenAICompatibleAdapter(std::shared_ptr<ILLMTransport> transport)
    : transport_(std::move(transport)) {}

bool OpenAICompatibleAdapter::init(const LLMAdapterConfig& config) {
  if (transport_ == nullptr || !config_has_required_values(config)) {
    config_.reset();
    return false;
  }

  config_ = config;
  return true;
}

AdapterCallResult OpenAICompatibleAdapter::generate(
    const contracts::LLMRequest& request) {
  if (!is_ready()) {
    return make_failure_result(ResultCode::RuntimeRetryExhausted,
                               "openai-compatible adapter is not initialized",
                               false,
                               std::string(kGenerateStage));
  }

  const auto validation = contracts::validate_llm_request_field_rules(request);
  if (!validation.ok || !request.request_mode.has_value() ||
      *request.request_mode != contracts::LLMRequestMode::Unary) {
    return make_failure_result(ResultCode::ValidationFieldMissing,
                               validation.ok
                                   ? "openai-compatible adapter only supports unary generate() in 025"
                                   : std::string(validation.reason),
                               false,
                               std::string(kGenerateStage));
  }

  const auto transport_request = make_chat_request(request, false);
  if (!transport_request.has_consistent_values()) {
    return make_failure_result(ResultCode::ValidationFieldMissing,
                               "openai-compatible adapter failed to materialize a valid transport request",
                               false,
                               std::string(kGenerateStage));
  }

  const auto transport_response = transport_->send(transport_request);
  if (!transport_response.has_consistent_values() || !transport_response.ok()) {
    return make_failure_result(ResultCode::ProviderTimeout,
                               "openai-compatible transport failure: " +
                                   resolve_transport_error_message(transport_response),
                               is_retryable_status(transport_response),
                               std::string(kGenerateStage));
  }

  return map_chat_response(request, transport_response);
}

StreamSessionRef OpenAICompatibleAdapter::stream_generate(
    const contracts::LLMRequest& request,
    IStreamObserver* observer) {
  StreamSessionRef session_ref;
  if (config_.has_value()) {
    session_ref.session_id = make_stream_session_id(*config_, request);
  }

  const auto fail_stream = [&](ResultCode result_code,
                               std::string message,
                               bool retryable,
                               std::string provider_code) {
    if (!provider_code.empty()) {
      message += " (" + provider_code + ")";
    }
    const auto failure = make_failure_result(result_code,
                                             std::move(message),
                                             retryable,
                                             std::string(kStreamGenerateStage));
    if (observer != nullptr && failure.error.has_value()) {
      observer->on_stream_failed(*failure.error, failure.result_code);
    }
    return session_ref;
  };

  if (!is_ready()) {
    return fail_stream(ResultCode::RuntimeRetryExhausted,
                       "openai-compatible adapter is not initialized",
                       false,
                       "adapter_uninitialized");
  }

  const auto validation = contracts::validate_llm_request_field_rules(request);
  if (!validation.ok || !request.request_mode.has_value() ||
      *request.request_mode != contracts::LLMRequestMode::Streaming) {
    return fail_stream(ResultCode::ValidationFieldMissing,
                       validation.ok
                           ? "openai-compatible adapter requires streaming request_mode"
                           : std::string(validation.reason),
                       false,
                       "request_invalid");
  }

  if (observer != nullptr) {
    auto feedback = observer->on_stream_session_started(session_ref);
    if (!feedback.has_consistent_values()) {
      feedback = StreamObserverFeedback::reject(
          ResultCode::RuntimeRetryExhausted,
          "stream observer returned inconsistent start feedback");
    }
    if (!feedback.proceed) {
      return fail_stream(feedback.result_code.value_or(ResultCode::RuntimeRetryExhausted),
                         feedback.message,
                         feedback.retryable,
                         "observer_rejected_session_start");
    }
  }

  const auto transport_request = make_chat_request(request, true);
  if (!transport_request.has_consistent_values()) {
    return fail_stream(ResultCode::ValidationFieldMissing,
                       "openai-compatible adapter failed to materialize a valid streaming transport request",
                       false,
                       "transport_request_invalid");
  }

  const auto transport_response = transport_->send(transport_request);
  if (!transport_response.has_consistent_values() || !transport_response.ok()) {
    return fail_stream(ResultCode::ProviderTimeout,
                       "openai-compatible streaming transport failure: " +
                           resolve_transport_error_message(transport_response),
                       is_retryable_status(transport_response),
                       "transport_failure");
  }

  const auto events = parse_sse_events(transport_response.body);
  if (events.empty()) {
    return fail_stream(ResultCode::ValidationFieldMissing,
                       "openai-compatible streaming response did not contain any SSE events",
                       false,
                       "stream_events_missing");
  }

  AdapterCallResult result;
  contracts::LLMResponse normalized_response;
  normalized_response.request_id = request.request_id;
  normalized_response.llm_call_id = request.llm_call_id;
  normalized_response.response_kind = contracts::LLMResponseKind::DirectResponse;
  normalized_response.completed_at = request.created_at;
  normalized_response.model_name = request.model_route.has_value()
                                       ? std::optional<std::string>(
                                             resolve_model_id(*request.model_route))
                                       : std::nullopt;
  normalized_response.prompt_id = request.prompt_id;
  normalized_response.prompt_version = request.prompt_version;

  std::string aggregated_content;
  std::string reasoning_content;
  std::vector<std::string> audit_tags;
  bool saw_done = false;
  auto push_audit_tag = [&](std::string tag) {
    if (std::find(audit_tags.begin(), audit_tags.end(), tag) == audit_tags.end()) {
      audit_tags.push_back(std::move(tag));
    }
  };

  for (const auto& event : events) {
    if (event == "[DONE]") {
      saw_done = true;
      continue;
    }

    if (result.provider_diagnostics.provider_trace_id.empty()) {
      result.provider_diagnostics.provider_trace_id =
          extract_json_string_field(event, "id").value_or("");
    }

    if (const auto model_name = extract_json_string_field(event, "model");
        model_name.has_value() && !model_name->empty()) {
      normalized_response.model_name = *model_name;
    }

    if (const auto finish_reason = extract_json_string_field(event, "finish_reason");
        finish_reason.has_value() && !finish_reason->empty()) {
      normalized_response.finish_reason = *finish_reason;
    }

    if (const auto reasoning = extract_json_string_field(event, "reasoning_content");
        reasoning.has_value() && !reasoning->empty()) {
      reasoning_content += *reasoning;
    }

    if (const auto tool_calls_json = extract_json_array_field(event, "tool_calls");
        tool_calls_json.has_value()) {
      normalized_response.response_kind = contracts::LLMResponseKind::ToolCallIntent;
      normalized_response.content_payload = build_tool_call_payload(*tool_calls_json);
      push_audit_tag("tool_calls_present");
    }

    if (const auto refusal = extract_json_string_field(event, "refusal");
        refusal.has_value() && !refusal->empty()) {
      normalized_response.response_kind = contracts::LLMResponseKind::Refusal;
      normalized_response.refusal_reason = *refusal;
      normalized_response.content_payload = *refusal;
      push_audit_tag("refusal_present");
    }

    if (const auto delta = extract_json_string_field(event, "content");
        delta.has_value() && !delta->empty()) {
      aggregated_content += *delta;
      if (observer != nullptr) {
        auto feedback = observer->on_stream_delta(*delta);
        if (!feedback.has_consistent_values()) {
          feedback = StreamObserverFeedback::reject(
              ResultCode::RuntimeRetryExhausted,
              "stream observer returned inconsistent delta feedback");
        }
        if (!feedback.proceed) {
          return fail_stream(
              feedback.result_code.value_or(ResultCode::RuntimeRetryExhausted),
              feedback.message,
              feedback.retryable,
              "observer_rejected_delta");
        }
      }
    }

    const auto prompt_tokens = extract_json_uint_field(event, "prompt_tokens");
    const auto completion_tokens = extract_json_uint_field(event, "completion_tokens");
    const auto total_tokens = extract_json_uint_field(event, "total_tokens");
    const auto prompt_cache_hit_tokens =
        extract_json_uint_field(event, "prompt_cache_hit_tokens");
    const auto prompt_cache_miss_tokens =
        extract_json_uint_field(event, "prompt_cache_miss_tokens");
    if (prompt_tokens.has_value() && completion_tokens.has_value() && total_tokens.has_value()) {
      normalized_response.input_tokens = prompt_tokens;
      normalized_response.output_tokens = completion_tokens;
      normalized_response.total_tokens = total_tokens;
      result.usage = AdapterUsageFragment{
          .prompt_tokens = prompt_tokens,
          .completion_tokens = completion_tokens,
          .total_tokens = total_tokens,
          .prompt_cache_hit_tokens = prompt_cache_hit_tokens,
          .prompt_cache_miss_tokens = prompt_cache_miss_tokens,
      };
    }
  }

  if (!reasoning_content.empty()) {
    result.provider_diagnostics.reasoning_content = reasoning_content;
    push_audit_tag("reasoning_content_present");
  }
  result.provider_diagnostics.audit_tags = audit_tags;

  if (!saw_done && !normalized_response.finish_reason.has_value()) {
    return fail_stream(ResultCode::ValidationFieldMissing,
                       "openai-compatible streaming response terminated without completion marker",
                       false,
                       "stream_incomplete");
  }

  if (saw_done && !normalized_response.finish_reason.has_value()) {
    normalized_response.finish_reason = "stop";
  }

  if (normalized_response.response_kind == contracts::LLMResponseKind::DirectResponse) {
    if (aggregated_content.empty()) {
      return fail_stream(ResultCode::ValidationFieldMissing,
                         "openai-compatible streaming response completed without content",
                         false,
                         "stream_content_missing");
    }
    normalized_response.content_payload = aggregated_content;
  }

  result.response = std::move(normalized_response);
  if (!result.has_consistent_values()) {
    return fail_stream(ResultCode::ValidationFieldMissing,
                       "openai-compatible adapter produced an inconsistent streaming result",
                       false,
                       "stream_result_inconsistent");
  }

  if (observer != nullptr) {
    observer->on_stream_completed(result);
  }
  return session_ref;
}

HealthStatus OpenAICompatibleAdapter::health_check() {
  if (!is_ready()) {
    return HealthStatus{
        .ready = false,
        .degraded = true,
        .message = "openai-compatible adapter is not initialized",
    };
  }

  const auto request = make_health_request();
  if (!request.has_consistent_values()) {
    return HealthStatus{
        .ready = false,
        .degraded = true,
        .message = "openai-compatible adapter failed to materialize health probe request",
    };
  }

  const auto response = transport_->send(request);
  if (!response.has_consistent_values()) {
    return HealthStatus{
        .ready = false,
        .degraded = true,
        .message = "openai-compatible health probe returned an inconsistent transport response",
    };
  }

  if (response.ok()) {
    return HealthStatus{
        .ready = true,
        .degraded = false,
        .message = "openai-compatible endpoint reachable",
    };
  }

  if (response.status_code == 429U || response.status_code == 503U) {
    return HealthStatus{
        .ready = true,
        .degraded = true,
        .message = "openai-compatible endpoint degraded: http " +
                   std::to_string(response.status_code),
    };
  }

  return HealthStatus{
      .ready = false,
      .degraded = true,
      .message = "openai-compatible endpoint unavailable: " +
                 resolve_transport_error_message(response),
  };
}

bool OpenAICompatibleAdapter::is_ready() const {
  return transport_ != nullptr && config_.has_value() && config_has_required_values(*config_);
}

std::string OpenAICompatibleAdapter::resolve_model_id(std::string_view route_key) const {
  if (route_key.empty()) {
    return {};
  }

  const std::size_t separator = route_key.rfind('/');
  if (separator == std::string_view::npos || separator + 1U >= route_key.size()) {
    return std::string(route_key);
  }

  return std::string(route_key.substr(separator + 1U));
}

LLMTransportRequest OpenAICompatibleAdapter::make_chat_request(
    const contracts::LLMRequest& request,
    bool streaming) const {
  if (!is_ready() || !request.messages.has_value() || !request.model_route.has_value()) {
    return {};
  }

  std::string body = "{\"model\":\"" + escape_json(resolve_model_id(*request.model_route)) +
                     "\",\"messages\":" + build_messages_json(*request.messages) +
                     ",\"stream\":" + std::string(streaming ? "true" : "false");
  if (request.max_output_tokens.has_value()) {
    body += ",\"max_tokens\":" + std::to_string(*request.max_output_tokens);
  }

  if (request.response_format.has_value() && *request.response_format == "json_object") {
    body += ",\"response_format\":{\"type\":\"json_object\"}";
  }

  if (streaming) {
    body += ",\"stream_options\":{\"include_usage\":true}";
  }

  body += "}";

  return LLMTransportRequest{
      .method = LLMTransportMethod::Post,
      .url = join_url(config_->base_url, kChatCompletionsPath),
      .auth_ref = config_->auth_ref,
      .header_refs = config_->header_refs,
      .base_url_alias = config_->base_url_alias,
      .snapshot_version = config_->snapshot_version,
      .headers = {
          LLMTransportHeader{.name = "Content-Type", .value = "application/json"},
          LLMTransportHeader{.name = "Accept",
                   .value = streaming ? "text/event-stream"
                            : "application/json"},
      },
      .body = std::move(body),
      .timeout_ms = config_->timeout_ms,
  };
}

LLMTransportRequest OpenAICompatibleAdapter::make_health_request() const {
  if (!is_ready()) {
    return {};
  }

  return LLMTransportRequest{
      .method = LLMTransportMethod::Get,
      .url = join_url(config_->base_url, kModelsPath),
      .auth_ref = config_->auth_ref,
      .header_refs = config_->header_refs,
      .base_url_alias = config_->base_url_alias,
      .snapshot_version = config_->snapshot_version,
      .headers = {
          LLMTransportHeader{.name = "Accept", .value = "application/json"},
      },
      .body = {},
      .timeout_ms = config_->timeout_ms,
  };
}

AdapterCallResult OpenAICompatibleAdapter::map_chat_response(
    const contracts::LLMRequest& request,
    const LLMTransportResponse& response) const {
  AdapterCallResult result;

  contracts::LLMResponse normalized_response;
  normalized_response.request_id = request.request_id;
  normalized_response.llm_call_id = request.llm_call_id;
  normalized_response.response_kind = contracts::LLMResponseKind::DirectResponse;
  normalized_response.completed_at = request.created_at;
  normalized_response.model_name = extract_json_string_field(response.body, "model");
  if (!normalized_response.model_name.has_value() && request.model_route.has_value()) {
    normalized_response.model_name = resolve_model_id(*request.model_route);
  }

  normalized_response.prompt_id = request.prompt_id;
  normalized_response.prompt_version = request.prompt_version;
  normalized_response.finish_reason = extract_json_string_field(response.body, "finish_reason");

  const auto tool_calls_json = extract_json_array_field(response.body, "tool_calls");
  const auto refusal = extract_json_string_field(response.body, "refusal");
  const auto content = extract_json_string_field(response.body, "content");
  if (tool_calls_json.has_value()) {
    normalized_response.response_kind = contracts::LLMResponseKind::ToolCallIntent;
    normalized_response.content_payload = build_tool_call_payload(*tool_calls_json);
    result.provider_diagnostics.audit_tags.push_back("tool_calls_present");
  } else if (refusal.has_value()) {
    normalized_response.response_kind = contracts::LLMResponseKind::Refusal;
    normalized_response.content_payload = *refusal;
    normalized_response.refusal_reason = *refusal;
    result.provider_diagnostics.audit_tags.push_back("refusal_present");
  } else if (content.has_value()) {
    normalized_response.content_payload = *content;
  }

  const auto prompt_tokens = extract_json_uint_field(response.body, "prompt_tokens");
  const auto completion_tokens = extract_json_uint_field(response.body, "completion_tokens");
  const auto total_tokens = extract_json_uint_field(response.body, "total_tokens");
  const auto prompt_cache_hit_tokens =
      extract_json_uint_field(response.body, "prompt_cache_hit_tokens");
  const auto prompt_cache_miss_tokens =
      extract_json_uint_field(response.body, "prompt_cache_miss_tokens");
  if (prompt_tokens.has_value() && completion_tokens.has_value() && total_tokens.has_value()) {
    normalized_response.input_tokens = prompt_tokens;
    normalized_response.output_tokens = completion_tokens;
    normalized_response.total_tokens = total_tokens;
    result.usage = AdapterUsageFragment{
        .prompt_tokens = prompt_tokens,
        .completion_tokens = completion_tokens,
        .total_tokens = total_tokens,
        .prompt_cache_hit_tokens = prompt_cache_hit_tokens,
        .prompt_cache_miss_tokens = prompt_cache_miss_tokens,
    };
  }

  result.provider_diagnostics.provider_trace_id =
      extract_json_string_field(response.body, "id").value_or("");
  result.provider_diagnostics.reasoning_content =
      extract_json_string_field(response.body, "reasoning_content").value_or("");
  if (!result.provider_diagnostics.reasoning_content.empty()) {
    result.provider_diagnostics.audit_tags.push_back("reasoning_content_present");
  }

  result.response = std::move(normalized_response);
  return result;
}

AdapterCallResult OpenAICompatibleAdapter::make_failure_result(
    contracts::ResultCode result_code,
    std::string message,
    bool retryable,
    std::string stage) const {
  contracts::ErrorInfo error;
  error.failure_type = contracts::classify_result_code(result_code);
  error.retryable = retryable;
  error.safe_to_replan = false;
  error.details.code = static_cast<int>(result_code);
  error.details.message = std::move(message);
  error.details.stage = std::move(stage);
  error.source_ref.ref_type = "adapter";
  error.source_ref.ref_id = config_.has_value() ? config_->adapter_id : "openai-compatible";

  AdapterCallResult result;
  result.error = std::move(error);
  result.result_code = result_code;
  return result;
}

}  // namespace dasall::llm
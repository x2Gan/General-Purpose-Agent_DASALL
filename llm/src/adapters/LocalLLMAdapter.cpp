#include "LocalLLMAdapter.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "llm/LLMBoundaryGuards.h"

namespace {

using AdapterCallResult = dasall::llm::AdapterCallResult;
using AdapterUsageFragment = dasall::llm::AdapterUsageFragment;
using HealthStatus = dasall::llm::HealthStatus;
using ILLMTransport = dasall::llm::ILLMTransport;
using LLMAdapterConfig = dasall::llm::LLMAdapterConfig;
using LLMTransportHeader = dasall::llm::LLMTransportHeader;
using LLMTransportMethod = dasall::llm::LLMTransportMethod;
using LLMTransportRequest = dasall::llm::LLMTransportRequest;
using LLMTransportResponse = dasall::llm::LLMTransportResponse;
using ResultCode = dasall::contracts::ResultCode;

constexpr std::string_view kLocalRuntimeFamily = "local_runtime";
constexpr std::string_view kGenerateStage = "llm.local_runtime.generate";
constexpr std::string_view kGeneratePath = "/generate";
constexpr std::string_view kHealthPath = "/health";

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
  std::vector<std::string> sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

[[nodiscard]] bool config_has_required_values(const LLMAdapterConfig& config) {
  return config.adapter_family == kLocalRuntimeFamily && !config.adapter_id.empty() &&
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
      {"developer:", "system"},
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

[[nodiscard]] std::string build_messages_json(const std::vector<std::string>& messages) {
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

[[nodiscard]] std::optional<std::string> extract_json_wrapped_field(
    std::string_view payload,
    std::string_view key,
    char open_character,
    char close_character,
    std::size_t offset = 0U) {
  const auto value_start = find_json_value_start(payload, key, offset);
  if (!value_start.has_value() || payload[*value_start] != open_character) {
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

    if (character == open_character) {
      ++depth;
    } else if (character == close_character) {
      --depth;
      if (depth == 0) {
        return std::string(payload.substr(*value_start, index - *value_start + 1U));
      }
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extract_json_array_field(
    std::string_view payload,
    std::string_view key,
    std::size_t offset = 0U) {
  return extract_json_wrapped_field(payload, key, '[', ']', offset);
}

[[nodiscard]] std::string build_tool_call_payload(std::string_view tool_calls_json) {
  return std::string("{\"tool_calls\":") + std::string(tool_calls_json) + "}";
}

[[nodiscard]] std::string resolve_transport_error_message(const LLMTransportResponse& response) {
  if (!response.error_message.empty()) {
    return response.error_message;
  }

  if (response.status_code == 0U) {
    return "transport did not return an HTTP status";
  }

  return "transport returned status " + std::to_string(response.status_code);
}

[[nodiscard]] bool is_retryable_status(const LLMTransportResponse& response) {
  return response.status_code == 0U || response.status_code == 429U ||
         response.status_code >= 500U;
}

}  // namespace

namespace dasall::llm {

LocalLLMAdapter::LocalLLMAdapter(std::shared_ptr<ILLMTransport> transport)
    : transport_(std::move(transport)) {}

bool LocalLLMAdapter::init(const LLMAdapterConfig& config) {
  if (transport_ == nullptr || !config_has_required_values(config)) {
    config_.reset();
    return false;
  }

  config_ = config;
  return true;
}

AdapterCallResult LocalLLMAdapter::generate(const contracts::LLMRequest& request) {
  if (!is_ready()) {
    return make_failure_result(ResultCode::RuntimeRetryExhausted,
                               "local runtime adapter is not initialized",
                               false,
                               std::string(kGenerateStage));
  }

  const auto validation = contracts::validate_llm_request_field_rules(request);
  if (!validation.ok || !request.request_mode.has_value() ||
      *request.request_mode != contracts::LLMRequestMode::Unary) {
    return make_failure_result(ResultCode::ValidationFieldMissing,
                               validation.ok
                                   ? "local runtime adapter only supports unary generate() in 027"
                                   : std::string(validation.reason),
                               false,
                               std::string(kGenerateStage));
  }

  const auto transport_request = make_generate_request(request);
  if (!transport_request.has_consistent_values()) {
    return make_failure_result(ResultCode::ValidationFieldMissing,
                               "local runtime adapter failed to materialize a valid transport request",
                               false,
                               std::string(kGenerateStage));
  }

  const auto transport_response = transport_->send(transport_request);
  if (!transport_response.has_consistent_values() || !transport_response.ok()) {
    return make_failure_result(ResultCode::ProviderTimeout,
                               "local runtime transport failure: " +
                                   resolve_transport_error_message(transport_response),
                               is_retryable_status(transport_response),
                               std::string(kGenerateStage));
  }

  return map_generate_response(request, transport_response);
}

StreamSessionRef LocalLLMAdapter::stream_generate(const contracts::LLMRequest&,
                                                  IStreamObserver*) {
  return StreamSessionRef{.session_id = "local-runtime-streaming-not-implemented"};
}

HealthStatus LocalLLMAdapter::health_check() {
  if (!is_ready()) {
    return HealthStatus{
        .ready = false,
        .degraded = true,
        .message = "local runtime adapter is not initialized",
    };
  }

  const auto request = make_health_request();
  if (!request.has_consistent_values()) {
    return HealthStatus{
        .ready = false,
        .degraded = true,
        .message = "local runtime adapter failed to materialize health probe request",
    };
  }

  const auto response = transport_->send(request);
  if (!response.has_consistent_values()) {
    return HealthStatus{
        .ready = false,
        .degraded = true,
        .message = "local runtime health probe returned an inconsistent transport response",
    };
  }

  if (response.ok()) {
    return HealthStatus{
        .ready = true,
        .degraded = false,
        .message = "local runtime endpoint reachable",
    };
  }

  if (response.status_code == 429U || response.status_code == 503U) {
    return HealthStatus{
        .ready = true,
        .degraded = true,
        .message = "local runtime endpoint degraded: status " +
                   std::to_string(response.status_code),
    };
  }

  return HealthStatus{
      .ready = false,
      .degraded = true,
      .message = "local runtime endpoint unavailable: " +
                 resolve_transport_error_message(response),
  };
}

bool LocalLLMAdapter::is_ready() const {
  return transport_ != nullptr && config_.has_value() && config_has_required_values(*config_);
}

std::string LocalLLMAdapter::resolve_model_id(std::string_view route_key) const {
  if (route_key.empty()) {
    return {};
  }

  const std::size_t separator = route_key.rfind('/');
  if (separator == std::string_view::npos || separator + 1U >= route_key.size()) {
    return std::string(route_key);
  }

  return std::string(route_key.substr(separator + 1U));
}

LLMTransportRequest LocalLLMAdapter::make_generate_request(
    const contracts::LLMRequest& request) const {
  if (!is_ready() || !request.messages.has_value() || !request.model_route.has_value()) {
    return {};
  }

  std::string body = "{\"model\":\"" + escape_json(resolve_model_id(*request.model_route)) +
                     "\",\"messages\":" + build_messages_json(*request.messages) +
                     ",\"stream\":false,\"execution_mode\":\"local_runtime\"";
  if (request.response_format.has_value()) {
    body += ",\"response_format\":\"" + escape_json(*request.response_format) + "\"";
  }

  if (request.max_output_tokens.has_value()) {
    body += ",\"max_output_tokens\":" +
            std::to_string(*request.max_output_tokens);
  }

  body += "}";

  return LLMTransportRequest{
      .method = LLMTransportMethod::Post,
      .url = join_url(config_->base_url, kGeneratePath),
      .auth_ref = config_->auth_ref,
      .header_refs = config_->header_refs,
      .base_url_alias = config_->base_url_alias,
      .snapshot_version = config_->snapshot_version,
      .headers = {
          LLMTransportHeader{.name = "Content-Type", .value = "application/json"},
          LLMTransportHeader{.name = "Accept", .value = "application/json"},
      },
      .body = std::move(body),
      .timeout_ms = config_->timeout_ms,
  };
}

LLMTransportRequest LocalLLMAdapter::make_health_request() const {
  if (!is_ready()) {
    return {};
  }

  return LLMTransportRequest{
      .method = LLMTransportMethod::Get,
      .url = join_url(config_->base_url, kHealthPath),
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

AdapterCallResult LocalLLMAdapter::map_generate_response(
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
  const auto refusal_reason = extract_json_string_field(response.body, "refusal_reason");
  const auto output_text = extract_json_string_field(response.body, "output_text");
  if (tool_calls_json.has_value()) {
    normalized_response.response_kind = contracts::LLMResponseKind::ToolCallIntent;
    normalized_response.content_payload = build_tool_call_payload(*tool_calls_json);
    result.provider_diagnostics.audit_tags.push_back("tool_calls_present");
  } else if (refusal_reason.has_value()) {
    normalized_response.response_kind = contracts::LLMResponseKind::Refusal;
    normalized_response.content_payload = *refusal_reason;
    normalized_response.refusal_reason = *refusal_reason;
    result.provider_diagnostics.audit_tags.push_back("refusal_present");
  } else if (output_text.has_value()) {
    normalized_response.content_payload = *output_text;
  }

  const auto input_tokens = extract_json_uint_field(response.body, "input_tokens");
  const auto output_tokens = extract_json_uint_field(response.body, "output_tokens");
  const auto total_tokens = extract_json_uint_field(response.body, "total_tokens");
  if (input_tokens.has_value() && output_tokens.has_value() && total_tokens.has_value()) {
    normalized_response.input_tokens = input_tokens;
    normalized_response.output_tokens = output_tokens;
    normalized_response.total_tokens = total_tokens;
    result.usage = AdapterUsageFragment{
        .prompt_tokens = input_tokens,
        .completion_tokens = output_tokens,
        .total_tokens = total_tokens,
        .prompt_cache_hit_tokens = std::nullopt,
        .prompt_cache_miss_tokens = std::nullopt,
    };
  }

  result.provider_diagnostics.provider_trace_id =
      extract_json_string_field(response.body, "runtime_session_id").value_or("");
  result.provider_diagnostics.reasoning_content =
      extract_json_string_field(response.body, "reasoning_trace").value_or("");
  if (!result.provider_diagnostics.reasoning_content.empty()) {
    result.provider_diagnostics.audit_tags.push_back("reasoning_content_present");
  }

  result.response = std::move(normalized_response);
  return result;
}

AdapterCallResult LocalLLMAdapter::make_failure_result(contracts::ResultCode result_code,
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
  error.source_ref.ref_id = config_.has_value() ? config_->adapter_id : "local-runtime";

  AdapterCallResult result;
  result.error = std::move(error);
  result.result_code = result_code;
  return result;
}

}  // namespace dasall::llm
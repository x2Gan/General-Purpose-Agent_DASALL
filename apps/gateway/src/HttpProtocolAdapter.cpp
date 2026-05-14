#include "HttpProtocolAdapter.h"

#include <memory>

#include "AccessSemanticKinds.h"
#include "ProtocolAdapterRegistry.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dasall::access::gateway {

namespace {

[[nodiscard]] dasall::access::PublishEnvelope make_success_fallback_envelope(
    const dasall::access::RuntimeDispatchResult& result) {
  dasall::access::PublishEnvelope fallback;
  fallback.protocol_status_hint = "202";
  fallback.result_id = result.receipt_ref.value_or("");
  fallback.payload = result.error_ref.value_or("");
  if (const auto request_id = result.response_context.find("request_id");
      request_id != result.response_context.end()) {
    fallback.request_id = request_id->second;
  }
  if (const auto trace_id = result.response_context.find("trace_id");
      trace_id != result.response_context.end()) {
    fallback.trace_id = trace_id->second;
  }
  return fallback;
}

[[nodiscard]] dasall::access::PublishEnvelope make_error_fallback_envelope(
    const dasall::access::RuntimeDispatchResult& result) {
  dasall::access::PublishEnvelope fallback;
  fallback.protocol_status_hint = "400";
  fallback.result_id = result.receipt_ref.value_or("");
  fallback.payload = result.error_ref.value_or("");
  if (const auto request_id = result.response_context.find("request_id");
      request_id != result.response_context.end()) {
    fallback.request_id = request_id->second;
  }
  if (const auto trace_id = result.response_context.find("trace_id");
      trace_id != result.response_context.end()) {
    fallback.trace_id = trace_id->second;
  }
  return fallback;
}

enum class JsonValueKind {
  String,
  Boolean,
  Other,
};

struct ParsedJsonValue {
  JsonValueKind kind = JsonValueKind::Other;
  std::string string_value;
  bool bool_value = false;
};

struct DecodedSubmitBody {
  std::string packet_id;
  std::string peer_ref;
  std::string payload;
  std::optional<std::string> trace_id;
  std::optional<std::string> session_hint;
  bool async_preferred = false;
  bool stream_requested = false;
};

void skip_ascii_ws(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
}

[[nodiscard]] std::string lower_ascii(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const unsigned char character : text) {
    lowered.push_back(static_cast<char>(std::tolower(character)));
  }
  return lowered;
}

[[nodiscard]] bool contains_header_injection(std::string_view text) {
  for (const char character : text) {
    if (character == '\r' || character == '\n' || character == '\0') {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::optional<std::string> find_header_value(
    const std::unordered_map<std::string, std::string>& headers,
    std::string_view key) {
  const std::string lowered_key = lower_ascii(key);
  for (const auto& [name, value] : headers) {
    if (lower_ascii(name) == lowered_key) {
      return value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool has_valid_json_content_type(
    const std::unordered_map<std::string, std::string>& headers) {
  const auto content_type = find_header_value(headers, "content-type");
  if (!content_type.has_value()) {
    return false;
  }

  const auto separator = content_type->find(';');
  const std::string media_type = lower_ascii(
      content_type->substr(0, separator == std::string::npos ? content_type->size()
                                                              : separator));
  return media_type == "application/json";
}

[[nodiscard]] bool headers_are_safe(
    const std::unordered_map<std::string, std::string>& headers) {
  std::size_t total_bytes = 0U;
  for (const auto& [name, value] : headers) {
    if (name.empty() || contains_header_injection(name) ||
        contains_header_injection(value)) {
      return false;
    }

    total_bytes += name.size() + value.size();
    if (name.size() > 8192U || value.size() > 8192U || total_bytes > 65536U) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_valid_idempotency_key(std::string_view key) {
  if (key.empty() || key.size() > 256U) {
    return false;
  }

  for (const unsigned char character : key) {
    if (std::isalnum(character) == 0 && character != '_' && character != '-') {
      return false;
    }
  }

  return true;
}

[[nodiscard]] std::optional<std::string> parse_json_string_token(
    std::string_view text,
    std::size_t& cursor) {
  if (cursor >= text.size() || text[cursor] != '"') {
    return std::nullopt;
  }

  std::string value;
  ++cursor;
  while (cursor < text.size()) {
    const char character = text[cursor++];
    if (character == '\\') {
      if (cursor >= text.size()) {
        return std::nullopt;
      }

      const char escaped = text[cursor++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          value.push_back(escaped);
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          return std::nullopt;
      }
      continue;
    }

    if (character == '"') {
      return value;
    }

    value.push_back(character);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<ParsedJsonValue> parse_json_value(
    std::string_view text,
    std::size_t& cursor) {
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size()) {
    return std::nullopt;
  }

  if (text[cursor] == '"') {
    const auto parsed = parse_json_string_token(text, cursor);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    return ParsedJsonValue{
        .kind = JsonValueKind::String,
        .string_value = *parsed,
        .bool_value = false,
    };
  }

  if (text.substr(cursor, 4) == "true") {
    cursor += 4U;
    return ParsedJsonValue{
        .kind = JsonValueKind::Boolean,
        .string_value = std::string(),
        .bool_value = true,
    };
  }
  if (text.substr(cursor, 5) == "false") {
    cursor += 5U;
    return ParsedJsonValue{
        .kind = JsonValueKind::Boolean,
        .string_value = std::string(),
        .bool_value = false,
    };
  }

  const std::size_t begin = cursor;
  int nested_braces = 0;
  int nested_brackets = 0;
  while (cursor < text.size()) {
    const char character = text[cursor];
    if (character == '{') {
      ++nested_braces;
    } else if (character == '}') {
      if (nested_braces == 0 && nested_brackets == 0) {
        break;
      }
      --nested_braces;
    } else if (character == '[') {
      ++nested_brackets;
    } else if (character == ']') {
      --nested_brackets;
    } else if (character == ',' && nested_braces == 0 && nested_brackets == 0) {
      break;
    }
    ++cursor;
  }

  if (cursor == begin) {
    return std::nullopt;
  }

  return ParsedJsonValue{
      .kind = JsonValueKind::Other,
      .string_value = std::string(),
      .bool_value = false,
  };
}

[[nodiscard]] std::optional<DecodedSubmitBody> parse_submit_body(std::string_view body) {
  std::size_t cursor = 0U;
  skip_ascii_ws(body, cursor);
  if (cursor >= body.size() || body[cursor] != '{') {
    return std::nullopt;
  }
  ++cursor;

  DecodedSubmitBody decoded;
  while (cursor < body.size()) {
    skip_ascii_ws(body, cursor);
    if (cursor < body.size() && body[cursor] == '}') {
      ++cursor;
      skip_ascii_ws(body, cursor);
      if (cursor != body.size()) {
        return std::nullopt;
      }
      return decoded;
    }

    const auto key = parse_json_string_token(body, cursor);
    if (!key.has_value()) {
      return std::nullopt;
    }

    skip_ascii_ws(body, cursor);
    if (cursor >= body.size() || body[cursor] != ':') {
      return std::nullopt;
    }
    ++cursor;

    const auto value = parse_json_value(body, cursor);
    if (!value.has_value()) {
      return std::nullopt;
    }

    if (*key == "packet_id") {
      if (value->kind != JsonValueKind::String) {
        return std::nullopt;
      }
      decoded.packet_id = value->string_value;
    } else if (*key == "peer_ref") {
      if (value->kind != JsonValueKind::String) {
        return std::nullopt;
      }
      decoded.peer_ref = value->string_value;
    } else if (*key == "payload") {
      if (value->kind != JsonValueKind::String) {
        return std::nullopt;
      }
      decoded.payload = value->string_value;
    } else if (*key == "trace_id") {
      if (value->kind != JsonValueKind::String) {
        return std::nullopt;
      }
      decoded.trace_id = value->string_value;
    } else if (*key == "session_hint") {
      if (value->kind != JsonValueKind::String) {
        return std::nullopt;
      }
      decoded.session_hint = value->string_value;
    } else if (*key == "async_preferred") {
      if (value->kind != JsonValueKind::Boolean) {
        return std::nullopt;
      }
      decoded.async_preferred = value->bool_value;
    } else if (*key == "stream_requested") {
      if (value->kind != JsonValueKind::Boolean) {
        return std::nullopt;
      }
      decoded.stream_requested = value->bool_value;
    }

    skip_ascii_ws(body, cursor);
    if (cursor < body.size() && body[cursor] == ',') {
      ++cursor;
      continue;
    }
    if (cursor < body.size() && body[cursor] == '}') {
      ++cursor;
      skip_ascii_ws(body, cursor);
      if (cursor != body.size()) {
        return std::nullopt;
      }
      return decoded;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::string escape_json(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
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

[[nodiscard]] HttpResponseContext make_error_response(const HttpDecodeError& error) {
  HttpResponseContext response;
  response.status_code = error.status_code;
  response.headers["Content-Type"] = "application/json";
  response.body = "{\"error\":\"" + escape_json(error.reason) +
                  "\",\"status\":\"" +
                  std::to_string(error.status_code) + "\"}";
  return response;
}

/// 将 PublishEnvelope 序列化为 JSON 字符串
std::string envelope_to_json(const dasall::access::PublishEnvelope& env) {
  std::string json = "{";

  auto append_kv = [&](const char* key, const std::string& val,
                        bool comma = true) {
    if (comma) {
      json += ',';
    }
    json += '"';
    json += key;
    json += "\":\"";
    json += escape_json(val);
    json += '"';
  };

  bool first = true;
  if (!env.result_id.empty()) {
    append_kv("result_id", env.result_id, !first);
    first = false;
  }
  if (!env.protocol_status_hint.empty()) {
    append_kv("status", env.protocol_status_hint, !first);
    first = false;
  }
  if (!env.payload.empty()) {
    append_kv("payload", env.payload, !first);
    first = false;
  }

  json += '}';
  return json;
}

}  // namespace

bool HttpProtocolAdapter::can_handle(std::string_view adapter_id,
                                     std::string_view transport_hint) const {
  return semantic::parse_access_entry_kind(adapter_id) ==
             semantic::AccessEntryKind::Gateway &&
         semantic::parse_access_protocol_kind(transport_hint) ==
             semantic::AccessProtocolKind::HttpUnary;
}

dasall::access::InboundPacket HttpProtocolAdapter::decode() {
  last_decode_error_.reset();
  dasall::access::InboundPacket packet;

  if (!active_request_.has_value() ||
      !active_request_->has_consistent_values()) {
    return packet;
  }

  if (active_request_->method != "POST" || active_request_->path != "/v1/submit") {
    return fail_decode(active_request_->method != "POST"
                           ? HttpDecodeErrorCode::InvalidMethod
                           : HttpDecodeErrorCode::InvalidPath,
                       active_request_->method != "POST" ? 405 : 404,
                       active_request_->method != "POST" ? "invalid_method"
                                                         : "invalid_path");
  }

  if (!headers_are_safe(active_request_->headers)) {
    return fail_decode(HttpDecodeErrorCode::InvalidHeader,
                       400,
                       "invalid_header");
  }

  if (!has_valid_json_content_type(active_request_->headers)) {
    return fail_decode(HttpDecodeErrorCode::InvalidContentType,
                       415,
                       "invalid_content_type");
  }

  if (active_request_->body.size() > max_request_body_bytes_) {
    return fail_decode(HttpDecodeErrorCode::PayloadTooLarge,
                       413,
                       "payload_too_large");
  }

  const auto decoded_body = parse_submit_body(active_request_->body);
  if (!decoded_body.has_value()) {
    return fail_decode(HttpDecodeErrorCode::MalformedJson,
                       400,
                       "malformed_json");
  }

  packet.packet_id = decoded_body->packet_id;
  packet.entry_type = std::string(semantic::to_string(semantic::AccessEntryKind::Gateway));
  packet.protocol_kind =
      std::string(semantic::to_string(semantic::AccessProtocolKind::HttpUnary));
  packet.peer_ref = decoded_body->peer_ref.empty()
                        ? std::string("http_remote")
                        : decoded_body->peer_ref;
  packet.payload = decoded_body->payload;
  packet.trace_id = decoded_body->trace_id;
  packet.session_hint = decoded_body->session_hint;
  packet.async_preferred = decoded_body->async_preferred;
  packet.stream_requested = decoded_body->stream_requested;

  if (const auto idempotency_key =
          find_header_value(active_request_->headers, "idempotency-key");
      idempotency_key.has_value()) {
    if (!is_valid_idempotency_key(*idempotency_key)) {
      return fail_decode(HttpDecodeErrorCode::InvalidIdempotencyKey,
                         400,
                         "invalid_idempotency_key");
    }
    packet.headers["idempotency_key"] = *idempotency_key;
  }

  return packet;
}

bool HttpProtocolAdapter::encode(
    const dasall::access::PublishEnvelope& envelope) {
  active_response_.body = envelope_to_json(envelope);
  active_response_.status_code = hint_to_status_code(
      envelope.protocol_status_hint);
  active_response_.headers["Content-Type"] = "application/json";
  return true;
}

void HttpProtocolAdapter::set_active_request(
    const HttpRequestContext& request) {
  active_request_ = request;
  // 重置上次响应
  active_response_ = HttpResponseContext{};
  last_decode_error_.reset();
}

void HttpProtocolAdapter::set_max_request_body_bytes(
    std::size_t max_request_body_bytes) {
  max_request_body_bytes_ = max_request_body_bytes;
}

const HttpResponseContext& HttpProtocolAdapter::active_response() const {
  return active_response_;
}

const std::optional<HttpDecodeError>& HttpProtocolAdapter::last_decode_error() const {
  return last_decode_error_;
}

dasall::access::InboundPacket HttpProtocolAdapter::fail_decode(
    HttpDecodeErrorCode code,
    int status_code,
    std::string reason) {
  last_decode_error_ = HttpDecodeError{
      .code = code,
      .status_code = status_code,
      .reason = std::move(reason),
  };
  return dasall::access::InboundPacket{};
}

int HttpProtocolAdapter::hint_to_status_code(std::string_view hint) {
  if (hint.empty()) {
    return 200;
  }
  int code = 0;
  const auto* end = hint.data() + hint.size();
  const auto [ptr, ec] = std::from_chars(hint.data(), end, code);
  if (ec != std::errc{} || code < 100 || code > 599) {
    return 500;
  }
  return code;
}

HttpResponseContext handle_submit_request(const HttpRequestContext& request,
                                          dasall::access::IAccessGateway& gateway,
                                          std::size_t max_request_body_bytes) {
  auto adapter = std::make_shared<HttpProtocolAdapter>();
  adapter->set_max_request_body_bytes(max_request_body_bytes);
  adapter->set_active_request(request);

  dasall::access::ProtocolAdapterRegistry registry;
  (void)registry.register_adapter(
      "gateway.submit.route",
      semantic::to_string(semantic::AccessEntryKind::Gateway),
      semantic::to_string(semantic::AccessProtocolKind::HttpUnary),
      adapter);

  const auto decoder = registry.resolve_decoder(
      semantic::to_string(semantic::AccessEntryKind::Gateway),
      semantic::to_string(semantic::AccessProtocolKind::HttpUnary));
  if (!decoder) {
    HttpResponseContext response;
    response.status_code = 500;
    response.body = R"({"error":"protocol_adapter_decoder_unavailable"})";
    response.headers["Content-Type"] = "application/json";
    return response;
  }

  const auto packet = decoder->decode();
  if (adapter->last_decode_error().has_value()) {
    return make_error_response(*adapter->last_decode_error());
  }

  const auto result = gateway.submit(packet);
  const auto encoder = registry.resolve_encoder(
      {.entry_type = std::string(semantic::to_string(semantic::AccessEntryKind::Gateway)),
       .protocol_kind = std::string(semantic::to_string(semantic::AccessProtocolKind::HttpUnary))});
  if (!encoder) {
    HttpResponseContext response;
    response.status_code = 500;
    response.body = R"({"error":"protocol_adapter_encoder_unavailable"})";
    response.headers["Content-Type"] = "application/json";
    return response;
  }

  if (result.publish_envelope.has_value()) {
    (void)encoder->encode(*result.publish_envelope);
    return adapter->active_response();
  }

  const auto fallback = result.disposition == dasall::access::AccessDisposition::AcceptedAsync
                            ? make_success_fallback_envelope(result)
                            : make_error_fallback_envelope(result);
  (void)encoder->encode(fallback);
  return adapter->active_response();
}

}  // namespace dasall::access::gateway

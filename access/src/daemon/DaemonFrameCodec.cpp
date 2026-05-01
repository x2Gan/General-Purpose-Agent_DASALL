#include "daemon/DaemonFrameCodec.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace dasall::access::daemon {
namespace {

struct JsonValue {
  enum class Kind {
    String,
    Bool,
    Integer,
    Object,
  };

  Kind kind = Kind::String;
  std::string string_value;
  bool bool_value = false;
  int integer_value = 0;
  std::unordered_map<std::string, JsonValue> object_value;
};

class JsonReader {
 public:
  explicit JsonReader(std::string_view input) : input_(input) {}

  [[nodiscard]] bool parse_root_object(
      std::unordered_map<std::string, JsonValue>& out) {
    skip_whitespace();
    if (!consume('{')) {
      return false;
    }

    skip_whitespace();
    if (consume('}')) {
      skip_whitespace();
      return at_end();
    }

    while (true) {
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_whitespace();
      if (!consume(':')) {
        return false;
      }

      JsonValue value;
      if (!parse_value(value)) {
        return false;
      }

      if (!out.emplace(key, std::move(value)).second) {
        return false;
      }

      skip_whitespace();
      if (consume('}')) {
        skip_whitespace();
        return at_end();
      }
      if (!consume(',')) {
        return false;
      }
      skip_whitespace();
    }
  }

 private:
  [[nodiscard]] bool parse_value(JsonValue& out) {
    skip_whitespace();
    if (at_end()) {
      return false;
    }

    const char current = input_[index_];
    if (current == '"') {
      out.kind = JsonValue::Kind::String;
      return parse_string(out.string_value);
    }

    if (current == '{') {
      out.kind = JsonValue::Kind::Object;
      return parse_object(out.object_value);
    }

    if (match_literal("true")) {
      out.kind = JsonValue::Kind::Bool;
      out.bool_value = true;
      return true;
    }

    if (match_literal("false")) {
      out.kind = JsonValue::Kind::Bool;
      out.bool_value = false;
      return true;
    }

    if (current == '-' || (current >= '0' && current <= '9')) {
      out.kind = JsonValue::Kind::Integer;
      return parse_integer(out.integer_value);
    }

    return false;
  }

  [[nodiscard]] bool parse_object(
      std::unordered_map<std::string, JsonValue>& out) {
    if (!consume('{')) {
      return false;
    }

    skip_whitespace();
    if (consume('}')) {
      return true;
    }

    while (true) {
      std::string key;
      if (!parse_string(key)) {
        return false;
      }
      skip_whitespace();
      if (!consume(':')) {
        return false;
      }

      JsonValue value;
      if (!parse_value(value)) {
        return false;
      }

      if (!out.emplace(key, std::move(value)).second) {
        return false;
      }

      skip_whitespace();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        return false;
      }
      skip_whitespace();
    }
  }

  [[nodiscard]] bool parse_string(std::string& out) {
    if (!consume('"')) {
      return false;
    }

    out.clear();
    while (!at_end()) {
      const unsigned char current =
          static_cast<unsigned char>(input_[index_++]);
      if (current == '"') {
        return true;
      }

      if (current == '\\') {
        if (at_end()) {
          return false;
        }

        const char escaped = input_[index_++];
        switch (escaped) {
          case '"':
            out.push_back('"');
            break;
          case '\\':
            out.push_back('\\');
            break;
          case '/':
            out.push_back('/');
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'u':
            if (!parse_unicode_escape(out)) {
              return false;
            }
            break;
          default:
            return false;
        }
        continue;
      }

      if (current < 0x20U) {
        return false;
      }

      out.push_back(static_cast<char>(current));
    }

    return false;
  }

  [[nodiscard]] bool parse_unicode_escape(std::string& out) {
    if (remaining() < 4U) {
      return false;
    }

    std::uint32_t code_point = 0U;
    for (int i = 0; i < 4; ++i) {
      const char digit = input_[index_++];
      code_point <<= 4U;
      if (digit >= '0' && digit <= '9') {
        code_point |= static_cast<std::uint32_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        code_point |= static_cast<std::uint32_t>(digit - 'a' + 10);
      } else if (digit >= 'A' && digit <= 'F') {
        code_point |= static_cast<std::uint32_t>(digit - 'A' + 10);
      } else {
        return false;
      }
    }

    if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
      return false;
    }

    append_utf8(code_point, out);
    return true;
  }

  [[nodiscard]] bool parse_integer(int& out) {
    const std::size_t start = index_;
    if (input_[index_] == '-') {
      ++index_;
      if (at_end()) {
        return false;
      }
    }

    if (input_[index_] == '0') {
      ++index_;
      if (!at_end() && input_[index_] >= '0' && input_[index_] <= '9') {
        return false;
      }
    } else {
      if (input_[index_] < '1' || input_[index_] > '9') {
        return false;
      }

      while (!at_end() && input_[index_] >= '0' && input_[index_] <= '9') {
        ++index_;
      }
    }

    const std::string_view integer_text = input_.substr(start, index_ - start);
    const auto parse_result = std::from_chars(integer_text.data(),
                                              integer_text.data() + integer_text.size(),
                                              out);
    return parse_result.ec == std::errc{} &&
           parse_result.ptr == integer_text.data() + integer_text.size();
  }

  static void append_utf8(const std::uint32_t code_point, std::string& out) {
    if (code_point <= 0x7FU) {
      out.push_back(static_cast<char>(code_point));
      return;
    }

    if (code_point <= 0x7FFU) {
      out.push_back(static_cast<char>(0xC0U | ((code_point >> 6U) & 0x1FU)));
      out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
      return;
    }

    out.push_back(static_cast<char>(0xE0U | ((code_point >> 12U) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }

  void skip_whitespace() {
    while (!at_end()) {
      const char current = input_[index_];
      if (current != ' ' && current != '\t' && current != '\n' &&
          current != '\r') {
        return;
      }
      ++index_;
    }
  }

  [[nodiscard]] bool consume(const char expected) {
    if (at_end() || input_[index_] != expected) {
      return false;
    }
    ++index_;
    return true;
  }

  [[nodiscard]] bool match_literal(std::string_view literal) {
    if (remaining() < literal.size()) {
      return false;
    }
    if (input_.substr(index_, literal.size()) != literal) {
      return false;
    }
    index_ += literal.size();
    return true;
  }

  [[nodiscard]] bool at_end() const {
    return index_ >= input_.size();
  }

  [[nodiscard]] std::size_t remaining() const {
    return input_.size() - index_;
  }

  std::string_view input_;
  std::size_t index_ = 0U;
};

[[nodiscard]] bool is_valid_utf8(std::string_view input) {
  std::size_t index = 0U;
  while (index < input.size()) {
    const unsigned char lead = static_cast<unsigned char>(input[index]);
    if (lead <= 0x7FU) {
      ++index;
      continue;
    }

    std::size_t width = 0U;
    std::uint32_t code_point = 0U;
    if ((lead & 0xE0U) == 0xC0U) {
      width = 2U;
      code_point = lead & 0x1FU;
      if (code_point == 0U) {
        return false;
      }
    } else if ((lead & 0xF0U) == 0xE0U) {
      width = 3U;
      code_point = lead & 0x0FU;
    } else if ((lead & 0xF8U) == 0xF0U) {
      width = 4U;
      code_point = lead & 0x07U;
    } else {
      return false;
    }

    if (index + width > input.size()) {
      return false;
    }

    for (std::size_t continuation = 1U; continuation < width; ++continuation) {
      const unsigned char byte =
          static_cast<unsigned char>(input[index + continuation]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | static_cast<std::uint32_t>(byte & 0x3FU);
    }

    if ((width == 2U && code_point < 0x80U) ||
        (width == 3U && code_point < 0x800U) ||
        (width == 4U && code_point < 0x10000U) ||
        code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }

    index += width;
  }

  return true;
}

[[nodiscard]] std::optional<std::string> string_field(
    const std::unordered_map<std::string, JsonValue>& object,
    std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end()) {
    return std::nullopt;
  }
  if (it->second.kind != JsonValue::Kind::String) {
    return std::nullopt;
  }
  return it->second.string_value;
}

[[nodiscard]] std::optional<bool> bool_field(
    const std::unordered_map<std::string, JsonValue>& object,
    std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end()) {
    return std::nullopt;
  }
  if (it->second.kind != JsonValue::Kind::Bool) {
    return std::nullopt;
  }
  return it->second.bool_value;
}

[[nodiscard]] std::optional<int> integer_field(
    const std::unordered_map<std::string, JsonValue>& object,
    std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end()) {
    return std::nullopt;
  }
  if (it->second.kind != JsonValue::Kind::Integer) {
    return std::nullopt;
  }
  return it->second.integer_value;
}

[[nodiscard]] bool has_only_known_request_fields(
    const std::unordered_map<std::string, JsonValue>& object) {
  for (const auto& [key, _] : object) {
    if (key != "schema_version" && key != "request_id" && key != "trace_id" &&
        key != "session_hint" && key != "idempotency_key" && key != "command" &&
        key != "args" && key != "payload" && key != "async_preference") {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_only_known_agent_result_fields(
    const std::unordered_map<std::string, JsonValue>& object) {
  for (const auto& [key, _] : object) {
    if (key != "result_id" && key != "response_text" && key != "request_id" &&
        key != "trace_id" && key != "task_completed") {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool has_only_known_response_fields(
    const std::unordered_map<std::string, JsonValue>& object) {
  for (const auto& [key, value] : object) {
    if (key != "schema_version" && key != "request_id" && key != "trace_id" &&
        key != "session_id" && key != "disposition" && key != "exit_code_hint" &&
        key != "receipt_ref" && key != "error_ref" && key != "agent_result") {
      return false;
    }

    if (key == "agent_result") {
      if (value.kind != JsonValue::Kind::Object ||
          !has_only_known_agent_result_fields(value.object_value)) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] bool parse_args_field(
    const std::unordered_map<std::string, JsonValue>& object,
    std::unordered_map<std::string, JsonValue>::const_iterator& args_it,
    std::map<std::string, std::string>& args_out) {
  args_it = object.find("args");
  if (args_it == object.end()) {
    return true;
  }

  if (args_it->second.kind != JsonValue::Kind::Object) {
    return false;
  }

  for (const auto& [key, value] : args_it->second.object_value) {
    if (value.kind != JsonValue::Kind::String) {
      return false;
    }
    args_out.emplace(key, value.string_value);
  }
  return true;
}

[[nodiscard]] std::string escape_json_string(std::string_view input) {
  std::string output;
  output.reserve(input.size() + 8U);
  for (const unsigned char current : input) {
    switch (current) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (current < 0x20U) {
          constexpr char kHex[] = "0123456789abcdef";
          output += "\\u00";
          output.push_back(kHex[(current >> 4U) & 0x0FU]);
          output.push_back(kHex[current & 0x0FU]);
        } else {
          output.push_back(static_cast<char>(current));
        }
        break;
    }
  }
  return output;
}

[[nodiscard]] std::string disposition_name(const UdsResponseDisposition disposition) {
  switch (disposition) {
    case UdsResponseDisposition::Rejected:
      return "rejected";
    case UdsResponseDisposition::Completed:
      return "completed";
    case UdsResponseDisposition::AcceptedAsync:
      return "accepted_async";
    case UdsResponseDisposition::NotReady:
      return "not_ready";
  }

  return "rejected";
}

[[nodiscard]] std::optional<UdsResponseDisposition> disposition_from_name(
    std::string_view disposition) {
  if (disposition == "rejected") {
    return UdsResponseDisposition::Rejected;
  }
  if (disposition == "completed") {
    return UdsResponseDisposition::Completed;
  }
  if (disposition == "accepted_async") {
    return UdsResponseDisposition::AcceptedAsync;
  }
  if (disposition == "not_ready") {
    return UdsResponseDisposition::NotReady;
  }

  return std::nullopt;
}

[[nodiscard]] std::string error_name(const DaemonFrameDecodeError error) {
  switch (error) {
    case DaemonFrameDecodeError::None:
      return "none";
    case DaemonFrameDecodeError::EmptyPayload:
      return "empty_payload";
    case DaemonFrameDecodeError::MissingSchemaVersion:
      return "missing_schema_version";
    case DaemonFrameDecodeError::UnsupportedSchemaVersion:
      return "unsupported_schema_version";
    case DaemonFrameDecodeError::MissingCommand:
      return "missing_command";
    case DaemonFrameDecodeError::UnknownCommand:
      return "unknown_command";
    case DaemonFrameDecodeError::PayloadTooLarge:
      return "payload_too_large";
    case DaemonFrameDecodeError::MalformedEnvelope:
      return "malformed_envelope";
  }

  return "malformed_envelope";
}

[[nodiscard]] int protocol_status_for_error(const DaemonFrameDecodeError error) {
  switch (error) {
    case DaemonFrameDecodeError::PayloadTooLarge:
      return 413;
    case DaemonFrameDecodeError::UnsupportedSchemaVersion:
      return 426;
    case DaemonFrameDecodeError::None:
      return 200;
    default:
      return 400;
  }
}

}  // namespace

DecodedDaemonRequestFrame decode_request_frame(
    std::string_view payload,
    const std::size_t max_payload_bytes) {
  DecodedDaemonRequestFrame result;

  if (payload.empty()) {
    result.error = DaemonFrameDecodeError::EmptyPayload;
    return result;
  }

  if (payload.size() > max_payload_bytes) {
    result.error = DaemonFrameDecodeError::PayloadTooLarge;
    return result;
  }

  if (!is_valid_utf8(payload)) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }

  std::unordered_map<std::string, JsonValue> root;
  JsonReader reader(payload);
  if (!reader.parse_root_object(root) || !has_only_known_request_fields(root)) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }

  const auto schema_version = string_field(root, "schema_version");
  if (!schema_version.has_value()) {
    result.error = DaemonFrameDecodeError::MissingSchemaVersion;
    return result;
  }
  if (*schema_version != kDaemonProtocolSchemaVersion) {
    result.error = DaemonFrameDecodeError::UnsupportedSchemaVersion;
    return result;
  }

  const auto command = string_field(root, "command");
  if (!command.has_value() || command->empty()) {
    result.error = DaemonFrameDecodeError::MissingCommand;
    return result;
  }
  if (!is_known_daemon_command(classify_daemon_command(*command))) {
    result.error = DaemonFrameDecodeError::UnknownCommand;
    return result;
  }

  std::unordered_map<std::string, JsonValue>::const_iterator args_it;
  if (!parse_args_field(root, args_it, result.frame.args)) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }

  result.frame.schema_version = *schema_version;
  result.frame.command = *command;
  result.frame.request_id = string_field(root, "request_id").value_or(std::string());
  result.frame.trace_id = string_field(root, "trace_id").value_or(std::string());
  result.frame.payload = string_field(root, "payload").value_or(std::string());
  if (result.frame.payload.size() > max_payload_bytes) {
    result.error = DaemonFrameDecodeError::PayloadTooLarge;
    return result;
  }

  if (const auto session_hint = string_field(root, "session_hint"); session_hint.has_value()) {
    result.frame.session_hint = *session_hint;
  }
  if (const auto idempotency_key = string_field(root, "idempotency_key");
      idempotency_key.has_value()) {
    result.frame.idempotency_key = *idempotency_key;
  }
  if (const auto async_preference = bool_field(root, "async_preference");
      async_preference.has_value() && *async_preference) {
    result.frame.async_preference = DaemonAsyncPreference::PreferAsync;
  }

  return result;
}

DecodedDaemonResponseFrame decode_response_frame(
    std::string_view payload,
    const std::size_t max_payload_bytes) {
  DecodedDaemonResponseFrame result;

  if (payload.empty()) {
    result.error = DaemonFrameDecodeError::EmptyPayload;
    return result;
  }

  if (payload.size() > max_payload_bytes) {
    result.error = DaemonFrameDecodeError::PayloadTooLarge;
    return result;
  }

  if (!is_valid_utf8(payload)) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }

  std::unordered_map<std::string, JsonValue> root;
  JsonReader reader(payload);
  if (!reader.parse_root_object(root) || !has_only_known_response_fields(root)) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }

  const auto schema_version = string_field(root, "schema_version");
  if (!schema_version.has_value()) {
    result.error = DaemonFrameDecodeError::MissingSchemaVersion;
    return result;
  }
  if (*schema_version != kDaemonProtocolSchemaVersion) {
    result.error = DaemonFrameDecodeError::UnsupportedSchemaVersion;
    return result;
  }

  const auto disposition = string_field(root, "disposition");
  if (!disposition.has_value() || disposition->empty()) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }
  const auto parsed_disposition = disposition_from_name(*disposition);
  if (!parsed_disposition.has_value()) {
    result.error = DaemonFrameDecodeError::MalformedEnvelope;
    return result;
  }

  result.frame.schema_version = *schema_version;
  result.frame.request_id = string_field(root, "request_id").value_or(std::string());
  result.frame.trace_id = string_field(root, "trace_id").value_or(std::string());
  result.frame.disposition = *parsed_disposition;

  if (const auto session_id = string_field(root, "session_id"); session_id.has_value()) {
    result.frame.session_id = *session_id;
  }
  if (const auto exit_code_hint = integer_field(root, "exit_code_hint");
      exit_code_hint.has_value()) {
    result.frame.exit_code_hint = *exit_code_hint;
  }
  if (const auto receipt_ref = string_field(root, "receipt_ref"); receipt_ref.has_value()) {
    result.frame.receipt_ref = *receipt_ref;
  }
  if (const auto error_ref = string_field(root, "error_ref"); error_ref.has_value()) {
    result.frame.error_ref = *error_ref;
  }

  const auto agent_result_it = root.find("agent_result");
  if (agent_result_it != root.end()) {
    dasall::contracts::AgentResult agent_result;
    const auto& agent_result_object = agent_result_it->second.object_value;

    if (const auto result_id = string_field(agent_result_object, "result_id");
        result_id.has_value()) {
      agent_result.result_id = *result_id;
    }
    if (const auto response_text = string_field(agent_result_object, "response_text");
        response_text.has_value()) {
      agent_result.response_text = *response_text;
    }
    if (const auto request_id = string_field(agent_result_object, "request_id");
        request_id.has_value()) {
      agent_result.request_id = *request_id;
    }
    if (const auto trace_id = string_field(agent_result_object, "trace_id");
        trace_id.has_value()) {
      agent_result.trace_id = *trace_id;
    }
    if (const auto task_completed = bool_field(agent_result_object, "task_completed");
        task_completed.has_value()) {
      agent_result.task_completed = *task_completed;
    }

    result.frame.agent_result = agent_result;
  }

  return result;
}

std::string encode_request_frame(const UdsRequestFrame& frame) {
  std::string output = "{";
  output += "\"schema_version\":\"" + escape_json_string(frame.schema_version) + "\"";
  output += ",\"request_id\":\"" + escape_json_string(frame.request_id) + "\"";
  output += ",\"trace_id\":\"" + escape_json_string(frame.trace_id) + "\"";

  if (frame.session_hint.has_value()) {
    output += ",\"session_hint\":\"" + escape_json_string(*frame.session_hint) + "\"";
  }
  if (frame.idempotency_key.has_value()) {
    output += ",\"idempotency_key\":\"" + escape_json_string(*frame.idempotency_key) + "\"";
  }

  output += ",\"command\":\"" + escape_json_string(frame.command) + "\"";
  output += ",\"args\":{";
  bool first_arg = true;
  for (const auto& [key, value] : frame.args) {
    if (!first_arg) {
      output += ',';
    }
    output += "\"" + escape_json_string(key) + "\":\"" +
              escape_json_string(value) + "\"";
    first_arg = false;
  }
  output += '}';
  output += ",\"payload\":\"" + escape_json_string(frame.payload) + "\"";
  output += ",\"async_preference\":";
  output += frame.async_preference == DaemonAsyncPreference::PreferAsync ? "true" : "false";
  output += '}';
  return output;
}

std::string encode_response_frame(const UdsResponseFrame& frame) {
  std::string output = "{";
  output += "\"schema_version\":\"" + escape_json_string(frame.schema_version) + "\"";
  output += ",\"request_id\":\"" + escape_json_string(frame.request_id) + "\"";
  output += ",\"trace_id\":\"" + escape_json_string(frame.trace_id) + "\"";
  output += ",\"disposition\":\"" + disposition_name(frame.disposition) + "\"";

  if (frame.session_id.has_value()) {
    output += ",\"session_id\":\"" + escape_json_string(*frame.session_id) + "\"";
  }
  if (frame.exit_code_hint.has_value()) {
    output += ",\"exit_code_hint\":" + std::to_string(*frame.exit_code_hint);
  }
  if (frame.receipt_ref.has_value()) {
    output += ",\"receipt_ref\":\"" + escape_json_string(*frame.receipt_ref) + "\"";
  }
  if (frame.error_ref.has_value()) {
    output += ",\"error_ref\":\"" + escape_json_string(*frame.error_ref) + "\"";
  }
  if (frame.agent_result.has_value()) {
    output += ",\"agent_result\":{";
    bool first_field = true;
    if (frame.agent_result->result_id.has_value()) {
      output += "\"result_id\":\"" +
                escape_json_string(*frame.agent_result->result_id) + "\"";
      first_field = false;
    }
    if (frame.agent_result->response_text.has_value()) {
      if (!first_field) {
        output += ',';
      }
      output += "\"response_text\":\"" +
                escape_json_string(*frame.agent_result->response_text) + "\"";
      first_field = false;
    }
    if (frame.agent_result->request_id.has_value()) {
      if (!first_field) {
        output += ',';
      }
      output += "\"request_id\":\"" +
                escape_json_string(*frame.agent_result->request_id) + "\"";
      first_field = false;
    }
    if (frame.agent_result->trace_id.has_value()) {
      if (!first_field) {
        output += ',';
      }
      output += "\"trace_id\":\"" +
                escape_json_string(*frame.agent_result->trace_id) + "\"";
      first_field = false;
    }
    if (frame.agent_result->task_completed.has_value()) {
      if (!first_field) {
        output += ',';
      }
      output += "\"task_completed\":";
      output += *frame.agent_result->task_completed ? "true" : "false";
    }
    output += '}';
  }

  output += '}';
  return output;
}

PublishEnvelope map_frame_error_to_publish_envelope(
    const DaemonFrameDecodeError error,
    std::string_view request_id,
    std::string_view trace_id) {
  PublishEnvelope envelope;
  envelope.request_id = std::string(request_id);
  envelope.trace_id = std::string(trace_id);
  envelope.result_id = envelope.request_id.empty()
                           ? std::string("frame-error")
                           : std::string("frame-error:") + envelope.request_id;
  envelope.protocol_kind = "ipc_uds";
  envelope.protocol_status_hint = std::to_string(protocol_status_for_error(error));
  envelope.payload = error_name(error);
  envelope.is_final = true;
  return envelope;
}

}  // namespace dasall::access::daemon
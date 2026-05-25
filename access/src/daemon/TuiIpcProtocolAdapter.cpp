#include "TuiIpcProtocolAdapter.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace dasall::access::daemon {

namespace {

struct JsonValue {
  enum class Kind {
    Null,
    String,
    Bool,
    Integer,
    Object,
    Array,
  };

  Kind kind = Kind::Null;
  std::string string_value;
  bool bool_value = false;
  int integer_value = 0;
  std::unordered_map<std::string, JsonValue> object_value;
  std::vector<JsonValue> array_value;
};

using JsonObject = std::unordered_map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonReader {
 public:
  explicit JsonReader(std::string_view input) : input_(input) {}

  [[nodiscard]] bool parse_root_object(JsonObject& out) {
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

      if (!out.emplace(std::move(key), std::move(value)).second) {
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
    if (current == '[') {
      out.kind = JsonValue::Kind::Array;
      return parse_array(out.array_value);
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
    if (match_literal("null")) {
      out.kind = JsonValue::Kind::Null;
      return true;
    }
    if (current == '-' || (current >= '0' && current <= '9')) {
      out.kind = JsonValue::Kind::Integer;
      return parse_integer(out.integer_value);
    }
    return false;
  }

  [[nodiscard]] bool parse_object(JsonObject& out) {
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

      if (!out.emplace(std::move(key), std::move(value)).second) {
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

  [[nodiscard]] bool parse_array(JsonArray& out) {
    if (!consume('[')) {
      return false;
    }

    skip_whitespace();
    if (consume(']')) {
      return true;
    }

    while (true) {
      JsonValue value;
      if (!parse_value(value)) {
        return false;
      }

      out.push_back(std::move(value));

      skip_whitespace();
      if (consume(']')) {
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
    for (int index = 0; index < 4; ++index) {
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

    if (code_point <= 0x7FU) {
      out.push_back(static_cast<char>(code_point));
      return true;
    }
    if (code_point <= 0x7FFU) {
      out.push_back(static_cast<char>(0xC0U | ((code_point >> 6U) & 0x1FU)));
      out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
      return true;
    }

    out.push_back(static_cast<char>(0xE0U | ((code_point >> 12U) & 0x0FU)));
    out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
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
    const auto parse_result = std::from_chars(
        integer_text.data(),
        integer_text.data() + integer_text.size(),
        out);
    return parse_result.ec == std::errc{} &&
           parse_result.ptr == integer_text.data() + integer_text.size();
  }

  void skip_whitespace() {
    while (!at_end()) {
      const char current = input_[index_];
      if (current != ' ' && current != '\t' && current != '\n' && current != '\r') {
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

[[nodiscard]] std::string payload_to_string(
    const std::vector<std::uint8_t>& payload) {
  if (payload.empty()) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
}

void append_escaped_string(std::string& out, std::string_view value) {
  out.push_back('"');
  for (const unsigned char current : value) {
    switch (current) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (current < 0x20U) {
          constexpr char kHexDigits[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHexDigits[(current >> 4U) & 0x0FU]);
          out.push_back(kHexDigits[current & 0x0FU]);
        } else {
          out.push_back(static_cast<char>(current));
        }
        break;
    }
  }
  out.push_back('"');
}

void append_member_prefix(std::string& out, bool& first) {
  if (!first) {
    out.push_back(',');
  }
  first = false;
}

void append_string_member(std::string& out,
                          std::string_view key,
                          std::string_view value,
                          bool& first) {
  append_member_prefix(out, first);
  append_escaped_string(out, key);
  out.push_back(':');
  append_escaped_string(out, value);
}

void append_integer_member(std::string& out,
                           std::string_view key,
                           const int value,
                           bool& first) {
  append_member_prefix(out, first);
  append_escaped_string(out, key);
  out.push_back(':');
  out += std::to_string(value);
}

void append_bool_member(std::string& out,
                        std::string_view key,
                        const bool value,
                        bool& first) {
  append_member_prefix(out, first);
  append_escaped_string(out, key);
  out.push_back(':');
  out += value ? "true" : "false";
}

void append_raw_member(std::string& out,
                       std::string_view key,
                       std::string_view raw_json,
                       bool& first) {
  append_member_prefix(out, first);
  append_escaped_string(out, key);
  out.push_back(':');
  out.append(raw_json.data(), raw_json.size());
}

void append_optional_string_member(std::string& out,
                                   std::string_view key,
                                   const std::optional<std::string>& value,
                                   bool& first) {
  if (!value.has_value()) {
    return;
  }
  append_string_member(out, key, *value, first);
}

void append_optional_bool_member(std::string& out,
                                 std::string_view key,
                                 const std::optional<bool>& value,
                                 bool& first) {
  if (!value.has_value()) {
    return;
  }
  append_bool_member(out, key, *value, first);
}

[[nodiscard]] std::string_view to_string(const TuiIpcOperation operation) {
  switch (operation) {
    case TuiIpcOperation::OpenSession:
      return "open_session";
    case TuiIpcOperation::SubmitTurn:
      return "submit_turn";
    case TuiIpcOperation::PollEvents:
      return "poll_events";
    case TuiIpcOperation::RouteCatalog:
      return "route_catalog";
    case TuiIpcOperation::CloseSession:
      return "close_session";
    case TuiIpcOperation::Unknown:
      return "unknown_operation";
  }
  return "unknown_operation";
}

[[nodiscard]] bool parse_operation(std::string_view encoded,
                                   TuiIpcOperation* out) {
  if (encoded == to_string(TuiIpcOperation::OpenSession)) {
    *out = TuiIpcOperation::OpenSession;
    return true;
  }
  if (encoded == to_string(TuiIpcOperation::SubmitTurn)) {
    *out = TuiIpcOperation::SubmitTurn;
    return true;
  }
  if (encoded == to_string(TuiIpcOperation::PollEvents)) {
    *out = TuiIpcOperation::PollEvents;
    return true;
  }
  if (encoded == to_string(TuiIpcOperation::RouteCatalog)) {
    *out = TuiIpcOperation::RouteCatalog;
    return true;
  }
  if (encoded == to_string(TuiIpcOperation::CloseSession)) {
    *out = TuiIpcOperation::CloseSession;
    return true;
  }
  *out = TuiIpcOperation::Unknown;
  return false;
}

[[nodiscard]] std::string outcome_to_string(const TuiIpcOutcome outcome) {
  return outcome == TuiIpcOutcome::Success ? "success" : "failure";
}

[[nodiscard]] std::string route_preference_mode_to_string(
    const TuiRoutePreferenceMode mode) {
  switch (mode) {
    case TuiRoutePreferenceMode::Auto:
      return "auto";
    case TuiRoutePreferenceMode::PreferDepth:
      return "prefer_depth";
    case TuiRoutePreferenceMode::PinModel:
      return "pin_model";
  }
  return "auto";
}

[[nodiscard]] bool parse_route_preference_mode(std::string_view encoded,
                                               TuiRoutePreferenceMode* out) {
  if (encoded == "auto") {
    *out = TuiRoutePreferenceMode::Auto;
    return true;
  }
  if (encoded == "prefer_depth") {
    *out = TuiRoutePreferenceMode::PreferDepth;
    return true;
  }
  if (encoded == "pin_model") {
    *out = TuiRoutePreferenceMode::PinModel;
    return true;
  }
  return false;
}

[[nodiscard]] const JsonValue* find_member(const JsonObject& object,
                                           std::string_view key) {
  const auto it = object.find(std::string(key));
  if (it == object.end()) {
    return nullptr;
  }
  return &it->second;
}

[[nodiscard]] bool read_required_string(const JsonObject& object,
                                        std::string_view key,
                                        std::string* out) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind != JsonValue::Kind::String) {
    return false;
  }
  *out = value->string_value;
  return true;
}

[[nodiscard]] bool read_optional_string(const JsonObject& object,
                                        std::string_view key,
                                        std::optional<std::string>* out) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind == JsonValue::Kind::Null) {
    *out = std::nullopt;
    return true;
  }
  if (value->kind != JsonValue::Kind::String) {
    return false;
  }
  *out = value->string_value;
  return true;
}

[[nodiscard]] bool read_required_bool(const JsonObject& object,
                                      std::string_view key,
                                      bool* out) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind != JsonValue::Kind::Bool) {
    return false;
  }
  *out = value->bool_value;
  return true;
}

[[nodiscard]] bool read_required_int(const JsonObject& object,
                                     std::string_view key,
                                     int* out) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind != JsonValue::Kind::Integer) {
    return false;
  }
  *out = value->integer_value;
  return true;
}

[[nodiscard]] const JsonObject* read_required_object(const JsonObject& object,
                                                     std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind != JsonValue::Kind::Object) {
    return nullptr;
  }
  return &value->object_value;
}

[[nodiscard]] std::string encode_string_array(
    const std::vector<std::string>& values) {
  std::string json = "[";
  bool first = true;
  for (const auto& value : values) {
    append_member_prefix(json, first);
    append_escaped_string(json, value);
  }
  json.push_back(']');
  return json;
}

[[nodiscard]] std::string encode_metadata(
    const std::vector<std::pair<std::string, std::string>>& metadata) {
  std::string json = "[";
  bool first = true;
  for (const auto& [key, value] : metadata) {
    append_member_prefix(json, first);
    bool object_first = true;
    json.push_back('{');
    append_string_member(json, "key", key, object_first);
    append_string_member(json, "value", value, object_first);
    json.push_back('}');
  }
  json.push_back(']');
  return json;
}

[[nodiscard]] bool decode_next_turn_preference(
    const JsonObject& object,
    TuiIpcNextTurnPreference* preference) {
  std::string encoded_mode;
  if (!read_required_string(object, "mode", &encoded_mode) ||
      !parse_route_preference_mode(encoded_mode, &preference->mode) ||
      !read_optional_string(
          object, "preferred_depth_tier", &preference->preferred_depth_tier) ||
      !read_optional_string(
          object, "pinned_provider_id", &preference->pinned_provider_id) ||
      !read_optional_string(
          object, "pinned_model_id", &preference->pinned_model_id) ||
      !read_required_string(
          object, "user_visible_summary", &preference->user_visible_summary) ||
      !read_required_string(object, "source", &preference->source) ||
      !read_required_bool(
          object,
          "applies_to_next_turn_only",
          &preference->applies_to_next_turn_only)) {
    return false;
  }
  return true;
}

[[nodiscard]] bool decode_request_payload(const JsonObject& object,
                                          const TuiIpcOperation operation,
                                          TuiIpcRequestPayload* payload) {
  switch (operation) {
    case TuiIpcOperation::OpenSession: {
      TuiIpcOpenSessionPayload decoded;
      return read_optional_string(object, "profile_id", &decoded.profile_id) &&
             read_optional_string(
                 object, "startup_mode_hint", &decoded.startup_mode_hint) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::SubmitTurn: {
      TuiIpcSubmitTurnPayload decoded;
      const JsonObject* next_preference =
          read_required_object(object, "next_preference");
      return read_required_string(object, "user_input", &decoded.user_input) &&
             next_preference != nullptr &&
             decode_next_turn_preference(*next_preference,
                                         &decoded.next_preference) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::PollEvents: {
      TuiIpcPollEventsPayload decoded;
      return read_optional_string(object, "event_cursor", &decoded.event_cursor) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::RouteCatalog: {
      TuiIpcRouteCatalogPayload decoded;
      return read_optional_string(object, "profile_id", &decoded.profile_id) &&
             read_optional_string(object, "selector_mode", &decoded.selector_mode) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::CloseSession: {
      TuiIpcCloseSessionPayload decoded;
      return read_required_string(object, "close_reason", &decoded.close_reason) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::Unknown:
      return false;
  }

  return false;
}

[[nodiscard]] bool validate_request(const TuiIpcRequestEnvelope& envelope) {
  if (envelope.request_id.empty() || envelope.trace_id.empty()) {
    return false;
  }

  switch (envelope.operation) {
    case TuiIpcOperation::OpenSession:
      return true;
    case TuiIpcOperation::SubmitTurn: {
      const auto* payload = std::get_if<TuiIpcSubmitTurnPayload>(&envelope.payload);
      return payload != nullptr && envelope.session_id.has_value() &&
             !envelope.session_id->empty() && !payload->user_input.empty();
    }
    case TuiIpcOperation::PollEvents:
      return envelope.session_id.has_value() && !envelope.session_id->empty();
    case TuiIpcOperation::RouteCatalog:
      return true;
    case TuiIpcOperation::CloseSession: {
      const auto* payload = std::get_if<TuiIpcCloseSessionPayload>(&envelope.payload);
      return payload != nullptr && envelope.session_id.has_value() &&
             !envelope.session_id->empty() && !payload->close_reason.empty();
    }
    case TuiIpcOperation::Unknown:
      return false;
  }

  return false;
}

[[nodiscard]] std::string encode_next_turn_preference(
    const TuiIpcNextTurnPreference& preference) {
  std::string json = "{";
  bool first = true;
  append_string_member(json,
                       "mode",
                       route_preference_mode_to_string(preference.mode),
                       first);
  append_optional_string_member(json,
                                "preferred_depth_tier",
                                preference.preferred_depth_tier,
                                first);
  append_optional_string_member(json,
                                "pinned_provider_id",
                                preference.pinned_provider_id,
                                first);
  append_optional_string_member(json,
                                "pinned_model_id",
                                preference.pinned_model_id,
                                first);
  append_string_member(json,
                       "user_visible_summary",
                       preference.user_visible_summary,
                       first);
  append_string_member(json, "source", preference.source, first);
  append_bool_member(json,
                     "applies_to_next_turn_only",
                     preference.applies_to_next_turn_only,
                     first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_session_view(const TuiIpcSessionView& session) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "session_id", session.session_id, first);
  append_string_member(json, "profile_id", session.profile_id, first);
  append_string_member(json, "daemon_readiness", session.daemon_readiness, first);
  append_string_member(json, "startup_mode", session.startup_mode, first);
  append_string_member(json, "started_at", session.started_at, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_turn_receipt(const TuiIpcTurnReceipt& receipt) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "request_id", receipt.request_id, first);
  append_string_member(json, "trace_id", receipt.trace_id, first);
  append_string_member(json, "session_id", receipt.session_id, first);
  append_string_member(json, "disposition", receipt.disposition, first);
  append_string_member(json, "receipt_ref", receipt.receipt_ref, first);
  append_string_member(json, "submitted_at", receipt.submitted_at, first);
  append_string_member(json, "summary_text", receipt.summary_text, first);
  append_optional_string_member(json, "response_text", receipt.response_text, first);
  append_optional_string_member(json, "reason_code", receipt.reason_code, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_status_projection(
    const TuiIpcStatusProjection& status) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "stage", status.stage, first);
  append_string_member(json, "current_tool", status.current_tool, first);
  append_string_member(json, "pending_interaction", status.pending_interaction, first);
  append_string_member(json, "budget_summary", status.budget_summary, first);
  append_string_member(json, "recovery_summary", status.recovery_summary, first);
  append_string_member(json, "health_summary", status.health_summary, first);
  append_string_member(json, "safe_mode_summary", status.safe_mode_summary, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_tool_summary(const TuiIpcToolSummary& summary) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "tool_name", summary.tool_name, first);
  append_string_member(json, "risk_summary", summary.risk_summary, first);
  append_string_member(json, "observation_summary", summary.observation_summary, first);
  if (summary.latency_ms.has_value()) {
    append_integer_member(json, "latency_ms", *summary.latency_ms, first);
  }
  append_raw_member(json, "badges", encode_string_array(summary.badges), first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_route_catalog_entry(
    const TuiIpcRouteCatalogEntry& entry) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "provider_id", entry.provider_id, first);
  append_string_member(json, "model_id", entry.model_id, first);
  append_string_member(json, "depth_tier", entry.depth_tier, first);
  append_string_member(json, "verification_state", entry.verification_state, first);
  append_string_member(json, "health", entry.health, first);
  append_bool_member(json, "profile_allowlisted", entry.profile_allowlisted, first);
  append_bool_member(json, "selectable", entry.selectable, first);
  append_raw_member(json,
                    "disabled_reasons",
                    encode_string_array(entry.disabled_reasons),
                    first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_model_route_projection(
    const TuiIpcModelRouteProjection& route) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "current_provider_id", route.current_provider_id, first);
  append_string_member(json, "current_model_id", route.current_model_id, first);
  append_string_member(json, "current_depth_tier", route.current_depth_tier, first);
  append_string_member(json, "verification_state", route.verification_state, first);
  append_string_member(json, "health", route.health, first);
  append_bool_member(json, "profile_allowlisted", route.profile_allowlisted, first);
  append_raw_member(json,
                    "disabled_reasons",
                    encode_string_array(route.disabled_reasons),
                    first);
  append_raw_member(json,
                    "next_preference",
                    encode_next_turn_preference(route.next_preference),
                    first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_route_catalog_view(
    const TuiIpcRouteCatalogView& route_catalog) {
  std::string json = "{";
  bool first = true;
  append_raw_member(json,
                    "current_route",
                    encode_model_route_projection(route_catalog.current_route),
                    first);

  std::string candidate_routes_json = "[";
  bool candidate_first = true;
  for (const auto& candidate : route_catalog.candidate_routes) {
    append_member_prefix(candidate_routes_json, candidate_first);
    candidate_routes_json += encode_route_catalog_entry(candidate);
  }
  candidate_routes_json.push_back(']');
  append_raw_member(json, "candidate_routes", candidate_routes_json, first);
  append_raw_member(json,
                    "disabled_reasons",
                    encode_string_array(route_catalog.disabled_reasons),
                    first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_event_projection(
    const TuiIpcEventProjection& event) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "event_cursor", event.event_cursor, first);
  append_string_member(json, "event_kind", event.event_kind, first);
  append_string_member(json, "session_id", event.session_id, first);
  append_string_member(json, "timestamp", event.timestamp, first);
  if (event.status_delta.has_value()) {
    append_raw_member(json,
                      "status_delta",
                      encode_status_projection(*event.status_delta),
                      first);
  }
  if (event.turn_receipt.has_value()) {
    append_raw_member(json, "turn_receipt", encode_turn_receipt(*event.turn_receipt), first);
  }
  if (event.tool_summary.has_value()) {
    append_raw_member(json, "tool_summary", encode_tool_summary(*event.tool_summary), first);
  }
  append_optional_string_member(json, "banner_reason", event.banner_reason, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_response_payload(
    const TuiIpcResponsePayload& payload,
    const TuiIpcOperation operation) {
  return std::visit(
      [operation](const auto& current) -> std::string {
        using Payload = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<Payload, TuiIpcSessionView>) {
          return encode_session_view(current);
        } else if constexpr (std::is_same_v<Payload, TuiIpcTurnReceipt>) {
          return encode_turn_receipt(current);
        } else if constexpr (std::is_same_v<Payload, TuiIpcPollEventsBatch>) {
          std::string json = "{";
          bool first = true;
          std::string events_json = "[";
          bool event_first = true;
          for (const auto& event : current.events) {
            append_member_prefix(events_json, event_first);
            events_json += encode_event_projection(event);
          }
          events_json.push_back(']');
          append_raw_member(json, "events", events_json, first);
          append_optional_string_member(json, "next_cursor", current.next_cursor, first);
          json.push_back('}');
          return json;
        } else if constexpr (std::is_same_v<Payload, TuiIpcRouteCatalogView>) {
          return encode_route_catalog_view(current);
        } else if constexpr (std::is_same_v<Payload, TuiIpcCloseSessionAck>) {
          std::string json = "{";
          bool first = true;
          append_bool_member(json, "closed", current.closed, first);
          json.push_back('}');
          return json;
        }

        std::string fallback = "{";
        bool first = true;
        append_string_member(fallback, "operation", std::string(to_string(operation)), first);
        fallback.push_back('}');
        return fallback;
      },
      payload);
}

[[nodiscard]] std::string encode_response_envelope(
    const TuiIpcResponseEnvelope& envelope) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "schema_version", envelope.schema_version, first);
  append_string_member(json, "operation", std::string(to_string(envelope.operation)), first);
  append_string_member(json, "request_id", envelope.request_id, first);
  append_string_member(json, "trace_id", envelope.trace_id, first);
  append_optional_string_member(json, "session_id", envelope.session_id, first);
  append_string_member(json, "outcome", outcome_to_string(envelope.outcome), first);
  if (envelope.payload.has_value()) {
    append_raw_member(json,
                      "payload",
                      encode_response_payload(*envelope.payload, envelope.operation),
                      first);
  }
  append_optional_string_member(json, "reason_domain", envelope.reason_domain, first);
  append_optional_string_member(json, "reason_code", envelope.reason_code, first);
  append_optional_string_member(json, "message", envelope.message, first);
  append_optional_bool_member(json, "retryable", envelope.retryable, first);
  append_optional_string_member(json, "error_ref", envelope.error_ref, first);
  if (!envelope.metadata.empty()) {
    append_raw_member(json, "metadata", encode_metadata(envelope.metadata), first);
  }
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string now_utc_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm utc_time{};
  gmtime_r(&now_time, &utc_time);

  std::array<char, 32> buffer{};
  const std::size_t written = std::strftime(
      buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc_time);
  if (written == 0U) {
    return "1970-01-01T00:00:00Z";
  }
  return std::string(buffer.data(), written);
}

[[nodiscard]] TuiIpcNextTurnPreference make_default_next_preference() {
  TuiIpcNextTurnPreference preference;
  preference.mode = TuiRoutePreferenceMode::Auto;
  preference.user_visible_summary = "auto";
  preference.source = "daemon";
  preference.applies_to_next_turn_only = true;
  return preference;
}

[[nodiscard]] TuiIpcRouteCatalogView build_default_route_catalog(
    std::string_view effective_profile_id) {
  TuiIpcRouteCatalogView route_catalog;
  route_catalog.current_route = TuiIpcModelRouteProjection{
      .current_provider_id = "daemon-local",
      .current_model_id = "dasall-core",
      .current_depth_tier = "balanced",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .disabled_reasons = {},
      .next_preference = make_default_next_preference(),
  };

  route_catalog.candidate_routes.push_back(TuiIpcRouteCatalogEntry{
      .provider_id = "daemon-local",
      .model_id = "dasall-core",
      .depth_tier = "balanced",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .selectable = true,
      .disabled_reasons = {},
  });
  route_catalog.candidate_routes.push_back(TuiIpcRouteCatalogEntry{
      .provider_id = "daemon-local",
      .model_id = "dasall-reasoning",
      .depth_tier = "deep",
      .verification_state = "verified",
      .health = "healthy",
      .profile_allowlisted = true,
      .selectable = true,
      .disabled_reasons = {},
  });
  if (effective_profile_id.empty()) {
    route_catalog.disabled_reasons.push_back("profile_missing");
  }
  return route_catalog;
}

[[nodiscard]] TuiIpcStatusProjection make_status_projection(
    std::string_view stage,
    std::string_view observation) {
  return TuiIpcStatusProjection{
      .stage = std::string(stage),
      .current_tool = "access.submit",
      .pending_interaction = "none",
      .budget_summary = "budget ok",
      .recovery_summary = "stable",
      .health_summary = "healthy",
      .safe_mode_summary = std::string(observation),
  };
}

[[nodiscard]] TuiIpcToolSummary make_tool_summary(std::string_view observation) {
  return TuiIpcToolSummary{
      .tool_name = "access.submit",
      .risk_summary = "low",
      .observation_summary = std::string(observation),
      .latency_ms = std::nullopt,
      .badges = {"local_daemon"},
  };
}

[[nodiscard]] TuiIpcResponseEnvelope make_failure_response(
    const DecodedTuiIpcRequest& decoded,
    std::string_view reason_domain,
    std::string_view reason_code,
    std::string_view message,
    const bool retryable = false) {
  TuiIpcResponseEnvelope response;
  response.operation = decoded.parsed_operation;
  response.request_id = decoded.request_id.empty() ? "tui-ipc-request" : decoded.request_id;
  response.trace_id = decoded.trace_id.empty() ? "trace:tui-ipc" : decoded.trace_id;
  response.session_id = decoded.session_id;
  response.outcome = TuiIpcOutcome::Failure;
  response.reason_domain = std::string(reason_domain);
  response.reason_code = std::string(reason_code);
  response.message = std::string(message);
  response.retryable = retryable;
  response.error_ref = response.request_id + ":" + std::string(reason_code);
  return response;
}

[[nodiscard]] TuiIpcResponseEnvelope make_decode_failure_response(
    const DecodedTuiIpcRequest& decoded) {
  switch (decoded.error) {
    case TuiIpcDecodeError::SchemaMismatch:
      return make_failure_response(decoded,
                                   "protocol",
                                   "schema_mismatch",
                                   "tui_ipc.v1 schema mismatch");
    case TuiIpcDecodeError::UnknownOperation:
      return make_failure_response(decoded,
                                   "protocol",
                                   "unknown_operation",
                                   "unknown tui_ipc.v1 operation");
    case TuiIpcDecodeError::ValidationRejected:
      return make_failure_response(decoded,
                                   "validation",
                                   "validation_failed",
                                   "tui_ipc.v1 request failed validation");
    case TuiIpcDecodeError::Malformed:
    case TuiIpcDecodeError::None:
      return make_failure_response(decoded,
                                   "protocol",
                                   "malformed_request",
                                   "malformed tui_ipc.v1 request envelope");
  }

  return make_failure_response(decoded,
                               "protocol",
                               "malformed_request",
                               "malformed tui_ipc.v1 request envelope");
}

[[nodiscard]] std::string next_session_id(TuiIpcSessionStore& session_store) {
  return "tui-session-" + std::to_string(++session_store.next_session_id);
}

[[nodiscard]] std::string next_event_cursor(TuiIpcSessionState& session_state) {
  ++session_state.next_event_cursor;
  return session_state.session.session_id + ":event:" +
         std::to_string(session_state.next_event_cursor);
}

[[nodiscard]] std::string dispatch_disposition_to_string(
    const AccessDisposition disposition) {
  switch (disposition) {
    case AccessDisposition::AcceptedAsync:
      return "accepted_async";
    case AccessDisposition::Completed:
      return "completed";
    case AccessDisposition::StreamAttached:
      return "stream_attached";
    case AccessDisposition::Rejected:
      return "rejected";
  }
  return "rejected";
}

[[nodiscard]] std::string receipt_summary_for_dispatch(
    const RuntimeDispatchResult& dispatch_result) {
  switch (dispatch_result.disposition) {
    case AccessDisposition::AcceptedAsync:
      return "queued for daemon-backed execution";
    case AccessDisposition::Completed:
      return "completed by daemon-backed execution";
    case AccessDisposition::StreamAttached:
      return "stream attached to daemon-backed execution";
    case AccessDisposition::Rejected:
      return "request rejected by daemon-backed access gateway";
  }
  return "request rejected by daemon-backed access gateway";
}

[[nodiscard]] std::optional<std::string> response_text_for_dispatch(
    const RuntimeDispatchResult& dispatch_result) {
  if (!dispatch_result.publish_envelope.has_value() ||
      !dispatch_result.publish_envelope->agent_result.has_value()) {
    return std::nullopt;
  }

  const auto& response_text =
      dispatch_result.publish_envelope->agent_result->response_text;
  if (!response_text.has_value() || response_text->empty()) {
    return std::nullopt;
  }

  return response_text;
}

[[nodiscard]] std::string extract_receipt_ref(
    const RuntimeDispatchResult& dispatch_result,
    std::string_view fallback_request_id) {
  if (dispatch_result.receipt_ref.has_value() && !dispatch_result.receipt_ref->empty()) {
    return *dispatch_result.receipt_ref;
  }
  if (dispatch_result.publish_envelope.has_value()) {
    const PublishEnvelope& envelope = *dispatch_result.publish_envelope;
    if (envelope.receipt.has_value() && !envelope.receipt->receipt_id.empty()) {
      return envelope.receipt->receipt_id;
    }
    if (!envelope.result_id.empty()) {
      return envelope.result_id;
    }
  }
  return std::string(fallback_request_id);
}

[[nodiscard]] InboundPacket build_submit_packet(
    const TuiIpcRequestEnvelope& envelope,
    const TuiIpcSubmitTurnPayload& payload,
    std::string_view peer_ref) {
  InboundPacket packet;
  packet.packet_id = envelope.request_id;
  packet.entry_type = "daemon";
  packet.protocol_kind = std::string(kTuiIpcSchemaVersion);
  packet.peer_ref = std::string(peer_ref);
  packet.payload = payload.user_input;
  packet.trace_id = envelope.trace_id;
  packet.session_hint = envelope.session_id;
  packet.async_preferred = true;
  packet.headers.emplace("tui_operation", "submit_turn");
  packet.headers.emplace(
      "tui_next_preference_mode",
      route_preference_mode_to_string(payload.next_preference.mode));
  if (payload.next_preference.preferred_depth_tier.has_value()) {
    packet.headers.emplace("tui_preferred_depth_tier",
                           *payload.next_preference.preferred_depth_tier);
  }
  if (payload.next_preference.pinned_provider_id.has_value()) {
    packet.headers.emplace("tui_pinned_provider_id",
                           *payload.next_preference.pinned_provider_id);
  }
  if (payload.next_preference.pinned_model_id.has_value()) {
    packet.headers.emplace("tui_pinned_model_id",
                           *payload.next_preference.pinned_model_id);
  }
  return packet;
}

[[nodiscard]] TuiIpcResponseEnvelope handle_open_session(
    const TuiIpcRequestEnvelope& envelope,
    TuiIpcSessionStore& session_store,
    std::string_view effective_profile_id) {
  const auto* payload = std::get_if<TuiIpcOpenSessionPayload>(&envelope.payload);
  if (payload == nullptr) {
    DecodedTuiIpcRequest failure;
    failure.request_id = envelope.request_id;
    failure.trace_id = envelope.trace_id;
    failure.parsed_operation = envelope.operation;
    return make_failure_response(failure,
                                 "validation",
                                 "validation_failed",
                                 "open_session payload is invalid");
  }

  std::lock_guard<std::mutex> lock(session_store.mutex);
  TuiIpcSessionState session_state;
  session_state.session.session_id = next_session_id(session_store);
  session_state.session.profile_id =
      payload->profile_id.value_or(std::string(effective_profile_id));
  session_state.session.daemon_readiness = "ready";
  session_state.session.startup_mode =
      payload->startup_mode_hint.value_or(std::string("daemon_backed"));
  session_state.session.started_at = now_utc_timestamp();
  session_state.route_catalog = build_default_route_catalog(effective_profile_id);

  TuiIpcResponseEnvelope response;
  response.operation = envelope.operation;
  response.request_id = envelope.request_id;
  response.trace_id = envelope.trace_id;
  response.session_id = session_state.session.session_id;
  response.outcome = TuiIpcOutcome::Success;
  response.payload = session_state.session;

  session_store.sessions.emplace(session_state.session.session_id,
                                 std::move(session_state));
  return response;
}

[[nodiscard]] TuiIpcResponseEnvelope handle_route_catalog(
    const TuiIpcRequestEnvelope& envelope,
    TuiIpcSessionStore& session_store,
    std::string_view effective_profile_id) {
  TuiIpcRouteCatalogView route_catalog = build_default_route_catalog(effective_profile_id);

  if (envelope.session_id.has_value() && !envelope.session_id->empty()) {
    std::lock_guard<std::mutex> lock(session_store.mutex);
    const auto it = session_store.sessions.find(*envelope.session_id);
    if (it == session_store.sessions.end()) {
      DecodedTuiIpcRequest failure;
      failure.request_id = envelope.request_id;
      failure.trace_id = envelope.trace_id;
      failure.session_id = envelope.session_id;
      failure.parsed_operation = envelope.operation;
      return make_failure_response(failure,
                                   "session",
                                   "session_not_found",
                                   "route_catalog session was not found");
    }
    route_catalog = it->second.route_catalog;
  }

  TuiIpcResponseEnvelope response;
  response.operation = envelope.operation;
  response.request_id = envelope.request_id;
  response.trace_id = envelope.trace_id;
  response.session_id = envelope.session_id;
  response.outcome = TuiIpcOutcome::Success;
  response.payload = std::move(route_catalog);
  return response;
}

[[nodiscard]] TuiIpcResponseEnvelope handle_poll_events(
    const TuiIpcRequestEnvelope& envelope,
    TuiIpcSessionStore& session_store) {
  std::lock_guard<std::mutex> lock(session_store.mutex);
  const auto it = session_store.sessions.find(*envelope.session_id);
  if (it == session_store.sessions.end()) {
    DecodedTuiIpcRequest failure;
    failure.request_id = envelope.request_id;
    failure.trace_id = envelope.trace_id;
    failure.session_id = envelope.session_id;
    failure.parsed_operation = envelope.operation;
    return make_failure_response(failure,
                                 "session",
                                 "session_not_found",
                                 "poll_events session was not found");
  }

  TuiIpcPollEventsBatch batch;
  batch.events.swap(it->second.pending_events);
  if (!batch.events.empty()) {
    batch.next_cursor = batch.events.back().event_cursor;
  }

  TuiIpcResponseEnvelope response;
  response.operation = envelope.operation;
  response.request_id = envelope.request_id;
  response.trace_id = envelope.trace_id;
  response.session_id = envelope.session_id;
  response.outcome = TuiIpcOutcome::Success;
  response.payload = std::move(batch);
  return response;
}

[[nodiscard]] TuiIpcResponseEnvelope handle_close_session(
    const TuiIpcRequestEnvelope& envelope,
    TuiIpcSessionStore& session_store) {
  std::lock_guard<std::mutex> lock(session_store.mutex);
  const auto removed = session_store.sessions.erase(*envelope.session_id);
  if (removed == 0U) {
    DecodedTuiIpcRequest failure;
    failure.request_id = envelope.request_id;
    failure.trace_id = envelope.trace_id;
    failure.session_id = envelope.session_id;
    failure.parsed_operation = envelope.operation;
    return make_failure_response(failure,
                                 "session",
                                 "session_not_found",
                                 "close_session session was not found");
  }

  TuiIpcResponseEnvelope response;
  response.operation = envelope.operation;
  response.request_id = envelope.request_id;
  response.trace_id = envelope.trace_id;
  response.session_id = envelope.session_id;
  response.outcome = TuiIpcOutcome::Success;
  response.payload = TuiIpcCloseSessionAck{.closed = true};
  return response;
}

[[nodiscard]] TuiIpcResponseEnvelope handle_submit_turn(
    const TuiIpcRequestEnvelope& envelope,
    IAccessGateway& gateway,
    TuiIpcSessionStore& session_store,
    std::string_view peer_ref) {
  const auto* payload = std::get_if<TuiIpcSubmitTurnPayload>(&envelope.payload);
  if (payload == nullptr) {
    DecodedTuiIpcRequest failure;
    failure.request_id = envelope.request_id;
    failure.trace_id = envelope.trace_id;
    failure.session_id = envelope.session_id;
    failure.parsed_operation = envelope.operation;
    return make_failure_response(failure,
                                 "validation",
                                 "validation_failed",
                                 "submit_turn payload is invalid");
  }

  {
    std::lock_guard<std::mutex> lock(session_store.mutex);
    if (session_store.sessions.find(*envelope.session_id) == session_store.sessions.end()) {
      DecodedTuiIpcRequest failure;
      failure.request_id = envelope.request_id;
      failure.trace_id = envelope.trace_id;
      failure.session_id = envelope.session_id;
      failure.parsed_operation = envelope.operation;
      return make_failure_response(failure,
                                   "session",
                                   "session_not_found",
                                   "submit_turn session was not found");
    }
  }

  const RuntimeDispatchResult dispatch_result =
      gateway.submit(build_submit_packet(envelope, *payload, peer_ref));
  if (dispatch_result.disposition == AccessDisposition::Rejected) {
    DecodedTuiIpcRequest failure;
    failure.request_id = envelope.request_id;
    failure.trace_id = envelope.trace_id;
    failure.session_id = envelope.session_id;
    failure.parsed_operation = envelope.operation;
    return make_failure_response(failure,
                                 "request",
                                 dispatch_result.error_ref.value_or(std::string("request_rejected")),
                                 "submit_turn rejected by access gateway");
  }

  TuiIpcTurnReceipt receipt;
  receipt.request_id = envelope.request_id;
  receipt.trace_id = envelope.trace_id;
  receipt.session_id = *envelope.session_id;
  receipt.disposition = dispatch_disposition_to_string(dispatch_result.disposition);
  receipt.receipt_ref = extract_receipt_ref(dispatch_result, envelope.request_id);
  receipt.submitted_at = now_utc_timestamp();
  receipt.summary_text = receipt_summary_for_dispatch(dispatch_result);
  receipt.response_text = response_text_for_dispatch(dispatch_result);

  TuiIpcEventProjection event;
  event.event_kind = "turn.receipt";
  event.session_id = *envelope.session_id;
  event.timestamp = receipt.submitted_at;
  event.turn_receipt = receipt;
  event.status_delta = make_status_projection(receipt.disposition, receipt.summary_text);
  event.tool_summary = make_tool_summary(receipt.summary_text);

  {
    std::lock_guard<std::mutex> lock(session_store.mutex);
    const auto it = session_store.sessions.find(*envelope.session_id);
    if (it != session_store.sessions.end()) {
      event.event_cursor = next_event_cursor(it->second);
      it->second.pending_events.push_back(event);
    }
  }

  TuiIpcResponseEnvelope response;
  response.operation = envelope.operation;
  response.request_id = envelope.request_id;
  response.trace_id = envelope.trace_id;
  response.session_id = envelope.session_id;
  response.outcome = TuiIpcOutcome::Success;
  response.payload = std::move(receipt);
  return response;
}

}  // namespace

TuiIpcProtocolAdapter::TuiIpcProtocolAdapter(
    std::shared_ptr<dasall::platform::IIPC> ipc)
    : ipc_(std::move(ipc)) {}

void TuiIpcProtocolAdapter::set_active_channel(
    dasall::platform::IpcChannelHandle channel,
    std::vector<std::uint8_t> payload) {
  active_channel_ = channel;
  active_payload_ = std::move(payload);
}

bool TuiIpcProtocolAdapter::payload_looks_like_tui_ipc() const {
  const std::string payload_text = payload_to_string(active_payload_);
  JsonObject root;
  JsonReader reader(payload_text);
  if (!reader.parse_root_object(root)) {
    return false;
  }

  const JsonValue* operation = find_member(root, "operation");
  return operation != nullptr && operation->kind == JsonValue::Kind::String;
}

DecodedTuiIpcRequest TuiIpcProtocolAdapter::decode_tui_ipc_request() const {
  DecodedTuiIpcRequest decoded;

  const std::string payload_text = payload_to_string(active_payload_);
  JsonObject root;
  JsonReader reader(payload_text);
  if (!reader.parse_root_object(root)) {
    decoded.error = TuiIpcDecodeError::Malformed;
    return decoded;
  }

  if (!read_required_string(root, "request_id", &decoded.request_id) ||
      !read_required_string(root, "trace_id", &decoded.trace_id) ||
      !read_optional_string(root, "session_id", &decoded.session_id)) {
    decoded.error = TuiIpcDecodeError::Malformed;
    return decoded;
  }

  std::string operation_string;
  if (!read_required_string(root, "operation", &operation_string)) {
    decoded.error = TuiIpcDecodeError::Malformed;
    return decoded;
  }
  const bool known_operation = parse_operation(operation_string, &decoded.parsed_operation);

  std::string schema_version;
  if (!read_required_string(root, "schema_version", &schema_version)) {
    decoded.error = TuiIpcDecodeError::Malformed;
    return decoded;
  }
  if (schema_version != kTuiIpcSchemaVersion) {
    decoded.error = TuiIpcDecodeError::SchemaMismatch;
    return decoded;
  }
  if (!known_operation) {
    decoded.error = TuiIpcDecodeError::UnknownOperation;
    return decoded;
  }

  int deadline_ms = 0;
  const JsonObject* payload = read_required_object(root, "payload");
  TuiIpcRequestEnvelope envelope;
  envelope.operation = decoded.parsed_operation;
  envelope.request_id = decoded.request_id;
  envelope.trace_id = decoded.trace_id;
  envelope.session_id = decoded.session_id;
  if (!read_required_int(root, "deadline_ms", &deadline_ms) || deadline_ms < 0 ||
      payload == nullptr || !decode_request_payload(*payload, envelope.operation, &envelope.payload) ||
      !validate_request(envelope)) {
    decoded.error = TuiIpcDecodeError::ValidationRejected;
    return decoded;
  }

  envelope.deadline_ms = static_cast<std::uint32_t>(deadline_ms);
  decoded.envelope = std::move(envelope);
  decoded.error = TuiIpcDecodeError::None;
  return decoded;
}

TuiIpcResponseEnvelope TuiIpcProtocolAdapter::dispatch_tui_ipc_operation(
    const DecodedTuiIpcRequest& decoded,
    dasall::access::IAccessGateway& gateway,
    TuiIpcSessionStore& session_store,
    std::string_view peer_ref,
    std::string_view effective_profile_id) const {
  if (!decoded.ok()) {
    return make_decode_failure_response(decoded);
  }

  const TuiIpcRequestEnvelope& envelope = *decoded.envelope;
  switch (envelope.operation) {
    case TuiIpcOperation::OpenSession:
      return handle_open_session(envelope, session_store, effective_profile_id);
    case TuiIpcOperation::SubmitTurn:
      return handle_submit_turn(envelope, gateway, session_store, peer_ref);
    case TuiIpcOperation::PollEvents:
      return handle_poll_events(envelope, session_store);
    case TuiIpcOperation::RouteCatalog:
      return handle_route_catalog(envelope, session_store, effective_profile_id);
    case TuiIpcOperation::CloseSession:
      return handle_close_session(envelope, session_store);
    case TuiIpcOperation::Unknown:
      return make_failure_response(decoded,
                                   "protocol",
                                   "unknown_operation",
                                   "unknown tui_ipc.v1 operation");
  }

  return make_failure_response(decoded,
                               "protocol",
                               "unknown_operation",
                               "unknown tui_ipc.v1 operation");
}

bool TuiIpcProtocolAdapter::encode_tui_ipc_response(
    const TuiIpcResponseEnvelope& envelope) const {
  if (!ipc_ || !active_channel_.has_consistent_values() ||
      !envelope.has_consistent_values()) {
    return false;
  }

  const std::string response_payload = encode_response_envelope(envelope);
  dasall::platform::IpcPayload payload;
  payload.reserve(response_payload.size());
  for (const char current : response_payload) {
    payload.push_back(static_cast<std::uint8_t>(current));
  }

  const auto send_result = ipc_->send(active_channel_, payload);
  return send_result.ok();
}

}  // namespace dasall::access::daemon
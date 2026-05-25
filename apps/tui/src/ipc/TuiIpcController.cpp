#include "ipc/TuiIpcController.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "IIPC.h"
#include "ipc/TuiIpcControllerTestHooks.h"
#include "linux/UnixIpcProvider.h"

namespace dasall::tui::ipc {

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

    const std::string_view integer_text =
        input_.substr(start, index_ - start);
    const auto parse_result =
        std::from_chars(integer_text.data(),
                        integer_text.data() + integer_text.size(),
                        out);
    return parse_result.ec == std::errc{} &&
           parse_result.ptr == integer_text.data() + integer_text.size();
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

  [[nodiscard]] bool at_end() const { return index_ >= input_.size(); }

  [[nodiscard]] std::size_t remaining() const {
    return input_.size() - index_;
  }

  std::string_view input_;
  std::size_t index_ = 0U;
};

enum class EnvelopeDecodeError {
  None,
  SchemaMismatch,
  Malformed,
};

struct DecodedResponseEnvelope {
  std::optional<TuiIpcResponseEnvelope> envelope;
  EnvelopeDecodeError error = EnvelopeDecodeError::Malformed;
  std::optional<std::string> actual_schema_version;
};

std::mutex g_test_ipc_mutex;
std::shared_ptr<dasall::platform::IIPC> g_test_ipc_override;

[[nodiscard]] dasall::platform::IpcPayload to_ipc_payload(
    std::string_view payload_text) {
  dasall::platform::IpcPayload ipc_payload;
  ipc_payload.reserve(payload_text.size());
  for (const char value : payload_text) {
    ipc_payload.push_back(static_cast<std::uint8_t>(value));
  }
  return ipc_payload;
}

[[nodiscard]] std::string from_ipc_payload(
    const dasall::platform::IpcPayload& payload) {
  if (payload.empty()) {
    return {};
  }

  return std::string(reinterpret_cast<const char*>(payload.data()),
                     payload.size());
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

void append_optional_payload_member(
    std::string& out,
    std::string_view key,
    const std::optional<TuiIpcResponsePayload>& value,
    std::string_view operation_json,
    bool& first);

[[nodiscard]] std::string route_preference_mode_to_string(
    const data::TuiRoutePreferenceMode mode) {
  switch (mode) {
    case data::TuiRoutePreferenceMode::Auto:
      return "auto";
    case data::TuiRoutePreferenceMode::PreferDepth:
      return "prefer_depth";
    case data::TuiRoutePreferenceMode::PinModel:
      return "pin_model";
  }

  return "auto";
}

[[nodiscard]] bool parse_route_preference_mode(
    std::string_view encoded,
    data::TuiRoutePreferenceMode* out) {
  if (encoded == "auto") {
    *out = data::TuiRoutePreferenceMode::Auto;
    return true;
  }
  if (encoded == "prefer_depth") {
    *out = data::TuiRoutePreferenceMode::PreferDepth;
    return true;
  }
  if (encoded == "pin_model") {
    *out = data::TuiRoutePreferenceMode::PinModel;
    return true;
  }
  return false;
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
  return false;
}

[[nodiscard]] std::string outcome_to_string(const TuiIpcOutcome outcome) {
  return outcome == TuiIpcOutcome::Success ? "success" : "failure";
}

[[nodiscard]] bool parse_outcome(std::string_view encoded,
                                 TuiIpcOutcome* out) {
  if (encoded == "success") {
    *out = TuiIpcOutcome::Success;
    return true;
  }
  if (encoded == "failure") {
    *out = TuiIpcOutcome::Failure;
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

[[nodiscard]] bool read_optional_bool(const JsonObject& object,
                                      std::string_view key,
                                      std::optional<bool>* out) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind == JsonValue::Kind::Null) {
    *out = std::nullopt;
    return true;
  }
  if (value->kind != JsonValue::Kind::Bool) {
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

[[nodiscard]] const JsonObject* read_optional_object(const JsonObject& object,
                                                     std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind == JsonValue::Kind::Null) {
    return nullptr;
  }
  if (value->kind != JsonValue::Kind::Object) {
    return nullptr;
  }
  return &value->object_value;
}

[[nodiscard]] const JsonArray* read_required_array(const JsonObject& object,
                                                   std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind != JsonValue::Kind::Array) {
    return nullptr;
  }
  return &value->array_value;
}

[[nodiscard]] const JsonArray* read_optional_array(const JsonObject& object,
                                                   std::string_view key) {
  const JsonValue* value = find_member(object, key);
  if (value == nullptr || value->kind == JsonValue::Kind::Null) {
    return nullptr;
  }
  if (value->kind != JsonValue::Kind::Array) {
    return nullptr;
  }
  return &value->array_value;
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

[[nodiscard]] std::string encode_next_turn_preference(
    const data::NextTurnPreference& preference) {
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

[[nodiscard]] std::string encode_session_view(
    const data::TuiSessionView& session) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "session_id", session.session_id, first);
  append_string_member(json, "profile_id", session.profile_id, first);
  append_string_member(
      json, "daemon_readiness", session.daemon_readiness, first);
  append_string_member(json, "startup_mode", session.startup_mode, first);
  append_string_member(json, "started_at", session.started_at, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_turn_receipt(
    const data::TuiTurnReceipt& receipt) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "request_id", receipt.request_id, first);
  append_string_member(json, "trace_id", receipt.trace_id, first);
  append_string_member(json, "session_id", receipt.session_id, first);
  append_string_member(json, "disposition", receipt.disposition, first);
  append_string_member(json, "receipt_ref", receipt.receipt_ref, first);
  append_string_member(json, "submitted_at", receipt.submitted_at, first);
  append_string_member(json, "summary_text", receipt.summary_text, first);
  append_optional_string_member(
      json, "response_text", receipt.response_text, first);
  append_optional_string_member(
      json, "reason_code", receipt.reason_code, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_status_projection(
    const data::TuiStatusProjection& status) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "stage", status.stage, first);
  append_string_member(json, "current_tool", status.current_tool, first);
  append_string_member(
      json, "pending_interaction", status.pending_interaction, first);
  append_string_member(json, "budget_summary", status.budget_summary, first);
  append_string_member(
      json, "recovery_summary", status.recovery_summary, first);
  append_string_member(json, "health_summary", status.health_summary, first);
  append_string_member(
      json, "safe_mode_summary", status.safe_mode_summary, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_tool_summary(
    const data::TuiToolSummaryView& summary) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "tool_name", summary.tool_name, first);
  append_string_member(json, "risk_summary", summary.risk_summary, first);
  append_string_member(
      json, "observation_summary", summary.observation_summary, first);
  if (summary.latency_ms.has_value()) {
    append_integer_member(json, "latency_ms", *summary.latency_ms, first);
  }
  append_raw_member(json, "badges", encode_string_array(summary.badges), first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_route_catalog_entry(
    const data::TuiRouteCatalogEntry& entry) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "provider_id", entry.provider_id, first);
  append_string_member(json, "model_id", entry.model_id, first);
  append_string_member(json, "depth_tier", entry.depth_tier, first);
  append_string_member(
    json, "verification_state", entry.verification_state, first);
  append_string_member(json, "health", entry.health, first);
  append_bool_member(
    json, "profile_allowlisted", entry.profile_allowlisted, first);
  append_bool_member(json, "selectable", entry.selectable, first);
  append_raw_member(
      json,
      "disabled_reasons",
      encode_string_array(entry.disabled_reasons),
      first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_model_route_projection(
    const data::TuiModelRouteProjection& route) {
  std::string json = "{";
  bool first = true;
  append_string_member(
      json, "current_provider_id", route.current_provider_id, first);
  append_string_member(json, "current_model_id", route.current_model_id, first);
  append_string_member(
      json, "current_depth_tier", route.current_depth_tier, first);
  append_string_member(
    json, "verification_state", route.verification_state, first);
  append_string_member(json, "health", route.health, first);
  append_bool_member(
    json, "profile_allowlisted", route.profile_allowlisted, first);
  append_raw_member(
      json,
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
    const data::TuiRouteCatalogView& route_catalog) {
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
    const data::TuiEventProjection& event) {
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
    append_raw_member(
        json, "turn_receipt", encode_turn_receipt(*event.turn_receipt), first);
  }
  if (event.tool_summary.has_value()) {
    append_raw_member(
        json, "tool_summary", encode_tool_summary(*event.tool_summary), first);
  }
  append_optional_string_member(json, "banner_reason", event.banner_reason, first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_request_payload(
    const TuiIpcRequestPayload& payload) {
  return std::visit(
      [](const auto& current) -> std::string {
        using Payload = std::decay_t<decltype(current)>;
        std::string json = "{";
        bool first = true;
        if constexpr (std::is_same_v<Payload, TuiIpcOpenSessionPayload>) {
          append_optional_string_member(
              json, "profile_id", current.profile_id, first);
          append_optional_string_member(
              json, "startup_mode_hint", current.startup_mode_hint, first);
        } else if constexpr (std::is_same_v<Payload, TuiIpcSubmitTurnPayload>) {
          append_string_member(json, "user_input", current.user_input, first);
          append_raw_member(json,
                            "next_preference",
                            encode_next_turn_preference(current.next_preference),
                            first);
        } else if constexpr (std::is_same_v<Payload, TuiIpcPollEventsPayload>) {
          append_optional_string_member(
              json, "event_cursor", current.event_cursor, first);
        } else if constexpr (std::is_same_v<Payload, TuiIpcRouteCatalogPayload>) {
          append_optional_string_member(
              json, "profile_id", current.profile_id, first);
          append_optional_string_member(
              json, "selector_mode", current.selector_mode, first);
        } else if constexpr (std::is_same_v<Payload, TuiIpcCloseSessionPayload>) {
          append_string_member(json, "close_reason", current.close_reason, first);
        }
        json.push_back('}');
        return json;
      },
      payload);
}

[[nodiscard]] std::string encode_response_payload(
    const TuiIpcResponsePayload& payload,
    const TuiIpcOperation operation) {
  return std::visit(
      [operation](const auto& current) -> std::string {
        using Payload = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<Payload, data::TuiSessionView>) {
          return encode_session_view(current);
        } else if constexpr (std::is_same_v<Payload, data::TuiTurnReceipt>) {
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
          append_optional_string_member(
              json, "next_cursor", current.next_cursor, first);
          json.push_back('}');
          return json;
        } else if constexpr (std::is_same_v<Payload, data::TuiRouteCatalogView>) {
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
        append_string_member(
            fallback, "operation", std::string(to_string(operation)), first);
        fallback.push_back('}');
        return fallback;
      },
      payload);
}

void append_optional_payload_member(
    std::string& out,
    std::string_view key,
    const std::optional<TuiIpcResponsePayload>& value,
    std::string_view operation_json,
    bool& first) {
  if (!value.has_value()) {
    return;
  }

  TuiIpcOperation operation = TuiIpcOperation::OpenSession;
  if (!parse_operation(operation_json, &operation)) {
    return;
  }
  append_raw_member(out, key, encode_response_payload(*value, operation), first);
}

[[nodiscard]] std::string encode_request_envelope(
    const TuiIpcRequestEnvelope& envelope) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "schema_version", envelope.schema_version, first);
  append_string_member(
      json, "operation", std::string(to_string(envelope.operation)), first);
  append_string_member(json, "request_id", envelope.request_id, first);
  append_string_member(json, "trace_id", envelope.trace_id, first);
  append_optional_string_member(json, "session_id", envelope.session_id, first);
  append_integer_member(
      json, "deadline_ms", static_cast<int>(envelope.deadline_ms), first);
  append_raw_member(json, "payload", encode_request_payload(envelope.payload), first);
  json.push_back('}');
  return json;
}

[[nodiscard]] std::string encode_response_envelope(
    const TuiIpcResponseEnvelope& envelope) {
  std::string json = "{";
  bool first = true;
  append_string_member(json, "schema_version", envelope.schema_version, first);
  append_string_member(
      json, "operation", std::string(to_string(envelope.operation)), first);
  append_string_member(json, "request_id", envelope.request_id, first);
  append_string_member(json, "trace_id", envelope.trace_id, first);
  append_optional_string_member(json, "session_id", envelope.session_id, first);
  append_string_member(
      json, "outcome", outcome_to_string(envelope.outcome), first);
  append_optional_payload_member(json,
                                 "payload",
                                 envelope.payload,
                                 to_string(envelope.operation),
                                 first);
  append_optional_string_member(
      json, "reason_domain", envelope.reason_domain, first);
  append_optional_string_member(
      json, "reason_code", envelope.reason_code, first);
  append_optional_string_member(json, "message", envelope.message, first);
  append_optional_bool_member(json, "retryable", envelope.retryable, first);
  append_optional_string_member(json, "error_ref", envelope.error_ref, first);
  if (!envelope.metadata.empty()) {
    append_raw_member(json, "metadata", encode_metadata(envelope.metadata), first);
  }
  json.push_back('}');
  return json;
}

[[nodiscard]] bool decode_metadata_array(
    const JsonArray& metadata_json,
    std::vector<std::pair<std::string, std::string>>* metadata) {
  metadata->clear();
  for (const auto& entry : metadata_json) {
    if (entry.kind != JsonValue::Kind::Object) {
      return false;
    }

    std::string key;
    std::string value;
    if (!read_required_string(entry.object_value, "key", &key) ||
        !read_required_string(entry.object_value, "value", &value)) {
      return false;
    }
    metadata->emplace_back(std::move(key), std::move(value));
  }
  return true;
}

[[nodiscard]] bool decode_string_array(const JsonArray& encoded,
                                       std::vector<std::string>* out) {
  out->clear();
  out->reserve(encoded.size());
  for (const auto& entry : encoded) {
    if (entry.kind != JsonValue::Kind::String) {
      return false;
    }
    out->push_back(entry.string_value);
  }
  return true;
}

[[nodiscard]] bool decode_next_turn_preference(
    const JsonObject& object,
    data::NextTurnPreference* preference) {
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
          object, "applies_to_next_turn_only", &preference->applies_to_next_turn_only)) {
    return false;
  }
  return true;
}

[[nodiscard]] bool decode_session_view(const JsonObject& object,
                                       data::TuiSessionView* session) {
  return read_required_string(object, "session_id", &session->session_id) &&
         read_required_string(object, "profile_id", &session->profile_id) &&
         read_required_string(
             object, "daemon_readiness", &session->daemon_readiness) &&
         read_required_string(object, "startup_mode", &session->startup_mode) &&
         read_required_string(object, "started_at", &session->started_at);
}

[[nodiscard]] bool decode_turn_receipt(const JsonObject& object,
                                       data::TuiTurnReceipt* receipt) {
  return read_required_string(object, "request_id", &receipt->request_id) &&
         read_required_string(object, "trace_id", &receipt->trace_id) &&
         read_required_string(object, "session_id", &receipt->session_id) &&
         read_required_string(object, "disposition", &receipt->disposition) &&
         read_required_string(object, "receipt_ref", &receipt->receipt_ref) &&
         read_required_string(object, "submitted_at", &receipt->submitted_at) &&
         read_required_string(object, "summary_text", &receipt->summary_text) &&
         read_optional_string(object, "response_text", &receipt->response_text) &&
         read_optional_string(object, "reason_code", &receipt->reason_code);
}

[[nodiscard]] bool decode_status_projection(
    const JsonObject& object,
    data::TuiStatusProjection* status) {
  return read_required_string(object, "stage", &status->stage) &&
         read_required_string(object, "current_tool", &status->current_tool) &&
         read_required_string(
             object, "pending_interaction", &status->pending_interaction) &&
         read_required_string(
             object, "budget_summary", &status->budget_summary) &&
         read_required_string(
             object, "recovery_summary", &status->recovery_summary) &&
         read_required_string(
             object, "health_summary", &status->health_summary) &&
         read_required_string(
             object, "safe_mode_summary", &status->safe_mode_summary);
}

[[nodiscard]] bool decode_tool_summary(
    const JsonObject& object,
    data::TuiToolSummaryView* summary) {
  if (!read_required_string(object, "tool_name", &summary->tool_name) ||
      !read_required_string(object, "risk_summary", &summary->risk_summary) ||
      !read_required_string(
          object, "observation_summary", &summary->observation_summary)) {
    return false;
  }

  const JsonValue* latency = find_member(object, "latency_ms");
  if (latency == nullptr || latency->kind == JsonValue::Kind::Null) {
    summary->latency_ms = std::nullopt;
  } else if (latency->kind == JsonValue::Kind::Integer) {
    summary->latency_ms = latency->integer_value;
  } else {
    return false;
  }

  const JsonArray* badges = read_required_array(object, "badges");
  return badges != nullptr && decode_string_array(*badges, &summary->badges);
}

[[nodiscard]] bool decode_route_catalog_entry(
    const JsonObject& object,
    data::TuiRouteCatalogEntry* entry) {
  if (!read_required_string(object, "provider_id", &entry->provider_id) ||
      !read_required_string(object, "model_id", &entry->model_id) ||
      !read_required_string(object, "depth_tier", &entry->depth_tier) ||
      !read_required_string(
          object, "verification_state", &entry->verification_state) ||
      !read_required_string(object, "health", &entry->health) ||
      !read_required_bool(
          object, "profile_allowlisted", &entry->profile_allowlisted) ||
      !read_required_bool(object, "selectable", &entry->selectable)) {
    return false;
  }

  const JsonArray* disabled = read_required_array(object, "disabled_reasons");
  return disabled != nullptr &&
         decode_string_array(*disabled, &entry->disabled_reasons);
}

[[nodiscard]] bool decode_model_route_projection(
    const JsonObject& object,
    data::TuiModelRouteProjection* route) {
  if (!read_required_string(
          object, "current_provider_id", &route->current_provider_id) ||
      !read_required_string(
          object, "current_model_id", &route->current_model_id) ||
      !read_required_string(
      object, "current_depth_tier", &route->current_depth_tier) ||
    !read_required_string(
      object, "verification_state", &route->verification_state) ||
    !read_required_string(object, "health", &route->health) ||
    !read_required_bool(
      object, "profile_allowlisted", &route->profile_allowlisted)) {
    return false;
  }

  const JsonArray* disabled = read_required_array(object, "disabled_reasons");
  const JsonObject* next_preference =
      read_required_object(object, "next_preference");
  return disabled != nullptr && next_preference != nullptr &&
         decode_string_array(*disabled, &route->disabled_reasons) &&
         decode_next_turn_preference(*next_preference, &route->next_preference);
}

[[nodiscard]] bool decode_route_catalog_view(
    const JsonObject& object,
    data::TuiRouteCatalogView* route_catalog) {
  const JsonObject* current_route = read_required_object(object, "current_route");
  const JsonArray* candidates = read_required_array(object, "candidate_routes");
  const JsonArray* disabled = read_required_array(object, "disabled_reasons");
  if (current_route == nullptr || candidates == nullptr || disabled == nullptr ||
      !decode_model_route_projection(*current_route,
                                     &route_catalog->current_route) ||
      !decode_string_array(*disabled, &route_catalog->disabled_reasons)) {
    return false;
  }

  route_catalog->candidate_routes.clear();
  route_catalog->candidate_routes.reserve(candidates->size());
  for (const auto& candidate : *candidates) {
    if (candidate.kind != JsonValue::Kind::Object) {
      return false;
    }
    data::TuiRouteCatalogEntry entry;
    if (!decode_route_catalog_entry(candidate.object_value, &entry)) {
      return false;
    }
    route_catalog->candidate_routes.push_back(std::move(entry));
  }
  return true;
}

[[nodiscard]] bool decode_event_projection(
    const JsonObject& object,
    data::TuiEventProjection* event) {
  if (!read_required_string(object, "event_cursor", &event->event_cursor) ||
      !read_required_string(object, "event_kind", &event->event_kind) ||
      !read_required_string(object, "session_id", &event->session_id) ||
      !read_required_string(object, "timestamp", &event->timestamp) ||
      !read_optional_string(object, "banner_reason", &event->banner_reason)) {
    return false;
  }

  event->status_delta = std::nullopt;
  if (const JsonObject* status_delta =
          read_optional_object(object, "status_delta");
      status_delta != nullptr) {
    data::TuiStatusProjection decoded;
    if (!decode_status_projection(*status_delta, &decoded)) {
      return false;
    }
    event->status_delta = std::move(decoded);
  }

  event->turn_receipt = std::nullopt;
  if (const JsonObject* receipt = read_optional_object(object, "turn_receipt");
      receipt != nullptr) {
    data::TuiTurnReceipt decoded;
    if (!decode_turn_receipt(*receipt, &decoded)) {
      return false;
    }
    event->turn_receipt = std::move(decoded);
  }

  event->tool_summary = std::nullopt;
  if (const JsonObject* summary = read_optional_object(object, "tool_summary");
      summary != nullptr) {
    data::TuiToolSummaryView decoded;
    if (!decode_tool_summary(*summary, &decoded)) {
      return false;
    }
    event->tool_summary = std::move(decoded);
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
             read_optional_string(
                 object, "selector_mode", &decoded.selector_mode) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::CloseSession: {
      TuiIpcCloseSessionPayload decoded;
      return read_required_string(object, "close_reason", &decoded.close_reason) &&
             ((*payload = std::move(decoded)), true);
    }
  }

  return false;
}

[[nodiscard]] bool decode_response_payload(const JsonObject& object,
                                           const TuiIpcOperation operation,
                                           TuiIpcResponsePayload* payload) {
  switch (operation) {
    case TuiIpcOperation::OpenSession: {
      data::TuiSessionView decoded;
      return decode_session_view(object, &decoded) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::SubmitTurn: {
      data::TuiTurnReceipt decoded;
      return decode_turn_receipt(object, &decoded) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::PollEvents: {
      TuiIpcPollEventsBatch decoded;
      const JsonArray* events = read_required_array(object, "events");
      if (events == nullptr ||
          !read_optional_string(object, "next_cursor", &decoded.next_cursor)) {
        return false;
      }
      decoded.events.reserve(events->size());
      for (const auto& event : *events) {
        if (event.kind != JsonValue::Kind::Object) {
          return false;
        }
        data::TuiEventProjection decoded_event;
        if (!decode_event_projection(event.object_value, &decoded_event)) {
          return false;
        }
        decoded.events.push_back(std::move(decoded_event));
      }
      *payload = std::move(decoded);
      return true;
    }
    case TuiIpcOperation::RouteCatalog: {
      data::TuiRouteCatalogView decoded;
      return decode_route_catalog_view(object, &decoded) &&
             ((*payload = std::move(decoded)), true);
    }
    case TuiIpcOperation::CloseSession: {
      TuiIpcCloseSessionAck decoded;
      return read_required_bool(object, "closed", &decoded.closed) &&
             ((*payload = std::move(decoded)), true);
    }
  }

  return false;
}

[[nodiscard]] std::optional<TuiIpcRequestEnvelope> decode_request_envelope(
    std::string_view payload_text) {
  JsonObject root;
  JsonReader reader(payload_text);
  if (!reader.parse_root_object(root)) {
    return std::nullopt;
  }

  TuiIpcRequestEnvelope envelope;
  std::string operation_string;
  int deadline_ms = 0;
  const JsonObject* payload = read_required_object(root, "payload");
  if (!read_required_string(root, "schema_version", &envelope.schema_version) ||
      envelope.schema_version != kTuiIpcSchemaVersion ||
      !read_required_string(root, "operation", &operation_string) ||
      !parse_operation(operation_string, &envelope.operation) ||
      !read_required_string(root, "request_id", &envelope.request_id) ||
      !read_required_string(root, "trace_id", &envelope.trace_id) ||
      !read_optional_string(root, "session_id", &envelope.session_id) ||
      !read_required_int(root, "deadline_ms", &deadline_ms) ||
      deadline_ms < 0 || payload == nullptr ||
      !decode_request_payload(*payload, envelope.operation, &envelope.payload)) {
    return std::nullopt;
  }

  envelope.deadline_ms = static_cast<std::uint32_t>(deadline_ms);
  return envelope;
}

[[nodiscard]] DecodedResponseEnvelope decode_response_envelope(
    std::string_view payload_text) {
  JsonObject root;
  JsonReader reader(payload_text);
  if (!reader.parse_root_object(root)) {
    return DecodedResponseEnvelope{
        .envelope = std::nullopt,
        .error = EnvelopeDecodeError::Malformed,
        .actual_schema_version = std::nullopt,
    };
  }

  TuiIpcResponseEnvelope envelope;
  std::string operation_string;
  std::string outcome_string;
  if (!read_required_string(root, "schema_version", &envelope.schema_version)) {
    return DecodedResponseEnvelope{
        .envelope = std::nullopt,
        .error = EnvelopeDecodeError::Malformed,
        .actual_schema_version = std::nullopt,
    };
  }

  if (envelope.schema_version != kTuiIpcSchemaVersion) {
    return DecodedResponseEnvelope{
        .envelope = std::nullopt,
        .error = EnvelopeDecodeError::SchemaMismatch,
        .actual_schema_version = envelope.schema_version,
    };
  }

  if (!read_required_string(root, "operation", &operation_string) ||
      !parse_operation(operation_string, &envelope.operation) ||
      !read_required_string(root, "request_id", &envelope.request_id) ||
      !read_required_string(root, "trace_id", &envelope.trace_id) ||
      !read_optional_string(root, "session_id", &envelope.session_id) ||
      !read_required_string(root, "outcome", &outcome_string) ||
      !parse_outcome(outcome_string, &envelope.outcome) ||
      !read_optional_string(root, "reason_domain", &envelope.reason_domain) ||
      !read_optional_string(root, "reason_code", &envelope.reason_code) ||
      !read_optional_string(root, "message", &envelope.message) ||
      !read_optional_bool(root, "retryable", &envelope.retryable) ||
      !read_optional_string(root, "error_ref", &envelope.error_ref)) {
    return DecodedResponseEnvelope{
        .envelope = std::nullopt,
        .error = EnvelopeDecodeError::Malformed,
        .actual_schema_version = std::nullopt,
    };
  }

  envelope.metadata.clear();
  if (const JsonArray* metadata = read_optional_array(root, "metadata");
      metadata != nullptr &&
      !decode_metadata_array(*metadata, &envelope.metadata)) {
    return DecodedResponseEnvelope{
        .envelope = std::nullopt,
        .error = EnvelopeDecodeError::Malformed,
        .actual_schema_version = std::nullopt,
    };
  }

  const JsonObject* payload = read_optional_object(root, "payload");
  if (envelope.outcome == TuiIpcOutcome::Success) {
    if (payload == nullptr) {
      return DecodedResponseEnvelope{
          .envelope = std::nullopt,
          .error = EnvelopeDecodeError::Malformed,
          .actual_schema_version = std::nullopt,
      };
    }

    TuiIpcResponsePayload decoded_payload;
    if (!decode_response_payload(*payload, envelope.operation, &decoded_payload)) {
      return DecodedResponseEnvelope{
          .envelope = std::nullopt,
          .error = EnvelopeDecodeError::Malformed,
          .actual_schema_version = std::nullopt,
      };
    }
    envelope.payload = std::move(decoded_payload);
  }

  if (!envelope.has_consistent_values()) {
    return DecodedResponseEnvelope{
        .envelope = std::nullopt,
        .error = EnvelopeDecodeError::Malformed,
        .actual_schema_version = std::nullopt,
    };
  }

  return DecodedResponseEnvelope{
      .envelope = std::move(envelope),
      .error = EnvelopeDecodeError::None,
      .actual_schema_version = std::nullopt,
  };
}

void append_base_metadata(data::TuiDataSourceIssue& issue,
                          const TuiIpcRequestEnvelope& request,
                          std::string_view socket_path) {
  issue.metadata.emplace_back("operation", std::string(to_string(request.operation)));
  issue.metadata.emplace_back("request_id", request.request_id);
  issue.metadata.emplace_back("trace_id", request.trace_id);
  issue.metadata.emplace_back("socket_path", std::string(socket_path));
  if (request.session_id.has_value()) {
    issue.metadata.emplace_back("session_id", *request.session_id);
  }
}

void append_unique_metadata(std::vector<std::pair<std::string, std::string>>& metadata,
                            std::string key,
                            std::string value) {
  for (const auto& [existing_key, existing_value] : metadata) {
    if (existing_key == key && existing_value == value) {
      return;
    }
  }
  metadata.emplace_back(std::move(key), std::move(value));
}

[[nodiscard]] data::TuiDataSourceIssue make_request_issue(
    std::string_view reason_code,
    std::string message,
    const TuiIpcRequestEnvelope& request,
    std::string_view socket_path) {
  data::TuiDataSourceIssue issue;
  issue.reason_domain = "request";
  issue.reason_code = std::string(reason_code);
  issue.message = std::move(message);
  issue.retryable = false;
  append_base_metadata(issue, request, socket_path);
  return issue;
}

[[nodiscard]] data::TuiDataSourceIssue make_transport_issue(
    std::string_view reason_code,
    std::string message,
    const bool retryable,
    const TuiIpcRequestEnvelope& request,
    std::string_view socket_path) {
  data::TuiDataSourceIssue issue;
  issue.reason_domain = "transport";
  issue.reason_code = std::string(reason_code);
  issue.message = std::move(message);
  issue.retryable = retryable;
  append_base_metadata(issue, request, socket_path);
  return issue;
}

[[nodiscard]] data::TuiDataSourceIssue make_protocol_issue(
    std::string_view reason_code,
    std::string message,
    const TuiIpcRequestEnvelope& request,
    std::string_view socket_path) {
  data::TuiDataSourceIssue issue;
  issue.reason_domain = "protocol";
  issue.reason_code = std::string(reason_code);
  issue.message = std::move(message);
  issue.retryable = false;
  append_base_metadata(issue, request, socket_path);
  return issue;
}

[[nodiscard]] data::TuiDataSourceIssue make_daemon_issue(
    std::string_view reason_code,
    std::string message,
    const bool retryable,
    const TuiIpcRequestEnvelope& request,
    std::string_view socket_path) {
  data::TuiDataSourceIssue issue;
  issue.reason_domain = "daemon";
  issue.reason_code = std::string(reason_code);
  issue.message = std::move(message);
  issue.retryable = retryable;
  append_base_metadata(issue, request, socket_path);
  return issue;
}

[[nodiscard]] data::TuiDataSourceIssue make_issue_from_platform_error(
    const dasall::platform::PlatformError& error,
    const TuiIpcRequestEnvelope& request,
    std::string_view socket_path) {
  data::TuiDataSourceIssue issue;
  switch (error.code) {
    case dasall::platform::PlatformErrorCode::InvalidArgument:
    case dasall::platform::PlatformErrorCode::PayloadTooLarge:
      issue = make_request_issue(
          "validation_failed", error.detail, request, socket_path);
      break;
    case dasall::platform::PlatformErrorCode::PermissionDenied:
      issue = make_transport_issue(
          "permission_denied", error.detail, false, request, socket_path);
      break;
    case dasall::platform::PlatformErrorCode::Timeout:
      issue = make_transport_issue(
          "timeout", error.detail, true, request, socket_path);
      break;
    case dasall::platform::PlatformErrorCode::PeerClosed:
    case dasall::platform::PlatformErrorCode::Disconnected:
      issue = make_transport_issue(
          "peer_closed", error.detail, error.retryable_hint, request, socket_path);
      break;
    case dasall::platform::PlatformErrorCode::NotFound:
      issue = make_transport_issue(
          "socket_missing", error.detail, false, request, socket_path);
      break;
    case dasall::platform::PlatformErrorCode::ConnectionRefused:
    case dasall::platform::PlatformErrorCode::ResourceExhausted:
    case dasall::platform::PlatformErrorCode::NoSpace:
    case dasall::platform::PlatformErrorCode::AddressInUse:
    case dasall::platform::PlatformErrorCode::InternalFailure:
    case dasall::platform::PlatformErrorCode::QueueClosed:
      issue = make_daemon_issue(
          "daemon_unavailable", error.detail, error.retryable_hint, request, socket_path);
      break;
  }

  if (!error.syscall_name.empty()) {
    append_unique_metadata(issue.metadata, "syscall", error.syscall_name);
  }
  if (error.errno_value.has_value()) {
    append_unique_metadata(
        issue.metadata, "errno", std::to_string(*error.errno_value));
  }
  return issue;
}

[[nodiscard]] bool has_required_context(
    const std::string& request_id,
    const std::string& trace_id) {
  return !request_id.empty() && !trace_id.empty();
}

[[nodiscard]] std::optional<data::TuiDataSourceIssue> validate_request(
    const data::TuiOpenSessionRequest& request,
    std::string_view socket_path,
    const TuiIpcRequestEnvelope& envelope) {
  if (!has_required_context(request.request_id, request.trace_id)) {
    return make_request_issue(
        "validation_failed",
        "open_session requires non-empty request_id and trace_id",
        envelope,
        socket_path);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<data::TuiDataSourceIssue> validate_request(
    const data::TuiSubmitTurnRequest& request,
    std::string_view socket_path,
    const TuiIpcRequestEnvelope& envelope) {
  if (!has_required_context(request.request_id, request.trace_id) ||
      request.session_id.empty() || request.user_input.empty()) {
    return make_request_issue(
        "validation_failed",
        "submit_turn requires session_id, user_input, request_id, and trace_id",
        envelope,
        socket_path);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<data::TuiDataSourceIssue> validate_request(
    const data::TuiPollEventsRequest& request,
    std::string_view socket_path,
    const TuiIpcRequestEnvelope& envelope) {
  if (!has_required_context(request.request_id, request.trace_id) ||
      request.session_id.empty()) {
    return make_request_issue(
        "validation_failed",
        "poll_events requires session_id, request_id, and trace_id",
        envelope,
        socket_path);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<data::TuiDataSourceIssue> validate_request(
    const data::TuiRouteCatalogRequest& request,
    std::string_view socket_path,
    const TuiIpcRequestEnvelope& envelope) {
  if (!has_required_context(request.request_id, request.trace_id)) {
    return make_request_issue(
        "validation_failed",
        "query_route_catalog requires non-empty request_id and trace_id",
        envelope,
        socket_path);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<data::TuiDataSourceIssue> validate_request(
    const data::TuiCloseSessionRequest& request,
    std::string_view socket_path,
    const TuiIpcRequestEnvelope& envelope) {
  if (!has_required_context(request.request_id, request.trace_id) ||
      request.session_id.empty() || request.close_reason.empty()) {
    return make_request_issue(
        "validation_failed",
        "close_session requires session_id, close_reason, request_id, and trace_id",
        envelope,
        socket_path);
  }
  return std::nullopt;
}

[[nodiscard]] std::shared_ptr<dasall::platform::IIPC> resolve_ipc_provider() {
  std::lock_guard<std::mutex> lock(g_test_ipc_mutex);
  if (g_test_ipc_override) {
    return g_test_ipc_override;
  }

  return std::make_shared<dasall::platform::linux::UnixIpcProvider>();
}

[[nodiscard]] std::int32_t clamp_deadline(const std::uint32_t deadline_ms) {
  constexpr auto kInt32Max =
      static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
  if (deadline_ms > kInt32Max) {
    return std::numeric_limits<std::int32_t>::max();
  }

  return static_cast<std::int32_t>(deadline_ms);
}

[[nodiscard]] std::uint32_t resolve_deadline(
    const TuiIpcOperation operation,
    const TuiIpcTimeoutPolicy& timeout_policy) {
  switch (operation) {
    case TuiIpcOperation::OpenSession:
      return timeout_policy.open_session_deadline_ms;
    case TuiIpcOperation::SubmitTurn:
      return timeout_policy.submit_turn_deadline_ms;
    case TuiIpcOperation::PollEvents:
      return timeout_policy.poll_events_deadline_ms;
    case TuiIpcOperation::RouteCatalog:
      return timeout_policy.route_catalog_deadline_ms;
    case TuiIpcOperation::CloseSession:
      return timeout_policy.close_session_deadline_ms;
  }

  return timeout_policy.open_session_deadline_ms;
}

struct RoundTripResult {
  std::optional<TuiIpcResponseEnvelope> response;
  std::optional<data::TuiDataSourceIssue> issue;
};

[[nodiscard]] RoundTripResult perform_roundtrip(
    const TuiIpcRequestEnvelope& request,
    const TuiIpcControllerOptions& options) {
  if (options.socket_path.empty()) {
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_transport_issue(
            "socket_missing",
            "tui ipc socket path is empty",
            false,
            request,
            options.socket_path),
    };
  }

  const auto ipc = resolve_ipc_provider();
  if (!ipc) {
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_daemon_issue(
            "daemon_unavailable",
            "tui ipc provider is unavailable",
            false,
            request,
            options.socket_path),
    };
  }

  dasall::platform::IpcEndpoint endpoint;
  endpoint.socket_path = options.socket_path;
  const std::int32_t deadline_ms = clamp_deadline(request.deadline_ms);

  const auto channel = ipc->connect(endpoint, deadline_ms);
  if (!channel.ok() || !channel.value.has_value()) {
    const auto error = channel.error.value_or(dasall::platform::PlatformError{
        .code = dasall::platform::PlatformErrorCode::InternalFailure,
        .category = dasall::platform::PlatformErrorCategory::IPC,
        .retryable_hint = false,
        .syscall_name = {},
        .errno_value = std::nullopt,
        .detail = "tui ipc connect failed",
    });
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_issue_from_platform_error(
            error, request, options.socket_path),
    };
  }

  const auto request_payload = to_ipc_payload(encode_request_envelope(request));
  const auto sent = ipc->send(*channel.value, request_payload);
  if (!sent.ok()) {
    (void)ipc->close(*channel.value);
    const auto error = sent.error.value_or(dasall::platform::PlatformError{
        .code = dasall::platform::PlatformErrorCode::InternalFailure,
        .category = dasall::platform::PlatformErrorCategory::IPC,
        .retryable_hint = false,
        .syscall_name = {},
        .errno_value = std::nullopt,
        .detail = "tui ipc send failed",
    });
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_issue_from_platform_error(
            error, request, options.socket_path),
    };
  }

  const auto received = ipc->receive(*channel.value, deadline_ms);
  (void)ipc->close(*channel.value);

  if (!received.ok() || !received.value.has_value()) {
    const auto error = received.error.value_or(dasall::platform::PlatformError{
        .code = dasall::platform::PlatformErrorCode::InternalFailure,
        .category = dasall::platform::PlatformErrorCategory::IPC,
        .retryable_hint = false,
        .syscall_name = {},
        .errno_value = std::nullopt,
        .detail = "tui ipc receive failed",
    });
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_issue_from_platform_error(
            error, request, options.socket_path),
    };
  }

  if (received.value->peer_closed) {
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_transport_issue(
            "peer_closed",
            "daemon closed channel before returning a response",
            false,
            request,
            options.socket_path),
    };
  }

  const std::string response_payload = from_ipc_payload(received.value->data);
  if (response_payload.empty()) {
    return RoundTripResult{
        .response = std::nullopt,
        .issue = make_protocol_issue(
            "malformed_response",
            "daemon returned an empty response envelope",
            request,
            options.socket_path),
    };
  }

  const DecodedResponseEnvelope decoded = decode_response_envelope(response_payload);
  if (!decoded.envelope.has_value()) {
    data::TuiDataSourceIssue issue = decoded.error == EnvelopeDecodeError::SchemaMismatch
                                         ? make_protocol_issue(
                                               "schema_mismatch",
                                               "daemon returned an incompatible tui ipc schema_version",
                                               request,
                                               options.socket_path)
                                         : make_protocol_issue(
                                               "malformed_response",
                                               "daemon returned an invalid tui ipc response envelope",
                                               request,
                                               options.socket_path);
    if (decoded.actual_schema_version.has_value()) {
      append_unique_metadata(
          issue.metadata, "actual_schema_version", *decoded.actual_schema_version);
    }
    append_unique_metadata(
        issue.metadata, "expected_schema_version", std::string(kTuiIpcSchemaVersion));
    return RoundTripResult{
        .response = std::nullopt,
        .issue = std::move(issue),
    };
  }

  if (decoded.envelope->operation != request.operation ||
      decoded.envelope->request_id != request.request_id ||
      decoded.envelope->trace_id != request.trace_id) {
    data::TuiDataSourceIssue issue = make_protocol_issue(
        "malformed_response",
        "daemon returned a response envelope that does not match the request context",
        request,
        options.socket_path);
    append_unique_metadata(
        issue.metadata,
        "response_operation",
        std::string(to_string(decoded.envelope->operation)));
    append_unique_metadata(issue.metadata,
                           "response_request_id",
                           decoded.envelope->request_id);
    append_unique_metadata(
        issue.metadata, "response_trace_id", decoded.envelope->trace_id);
    return RoundTripResult{
        .response = std::nullopt,
        .issue = std::move(issue),
    };
  }

  return RoundTripResult{
      .response = std::move(decoded.envelope),
      .issue = std::nullopt,
  };
}

[[nodiscard]] data::TuiDataSourceIssue make_issue_from_response(
    const TuiIpcResponseEnvelope& response,
    const TuiIpcRequestEnvelope& request,
    std::string_view socket_path) {
  data::TuiDataSourceIssue issue;
  issue.reason_domain = response.reason_domain.value_or("protocol");
  issue.reason_code = response.reason_code.value_or("malformed_response");
  issue.message = response.message.value_or(std::string());
  issue.retryable = response.retryable.value_or(false);
  issue.error_ref = response.error_ref;
  issue.metadata = response.metadata;
  append_base_metadata(issue, request, socket_path);
  append_unique_metadata(issue.metadata, "response_request_id", response.request_id);
  append_unique_metadata(issue.metadata, "response_trace_id", response.trace_id);
  if (response.session_id.has_value()) {
    append_unique_metadata(issue.metadata, "response_session_id", *response.session_id);
  }
  return issue;
}

template <typename TResult>
[[nodiscard]] TResult result_from_issue(data::TuiDataSourceIssue issue) {
  TResult result;
  result.issue = std::move(issue);
  return result;
}

[[nodiscard]] data::TuiPollEventsResult poll_result_from_issue(
    data::TuiDataSourceIssue issue) {
  data::TuiPollEventsResult result;
  result.issue = std::move(issue);
  return result;
}

}  // namespace

TuiIpcController::TuiIpcController(TuiIpcControllerOptions options)
    : options_(std::move(options)) {}

data::TuiOpenSessionResult TuiIpcController::open_session(
    const data::TuiOpenSessionRequest& request) {
  const TuiIpcRequestEnvelope envelope =
      make_request_envelope(request,
                            resolve_deadline(TuiIpcOperation::OpenSession,
                                             options_.timeout_policy));
  if (const auto validation_issue =
          validate_request(request, options_.socket_path, envelope);
      validation_issue.has_value()) {
    return result_from_issue<data::TuiOpenSessionResult>(*validation_issue);
  }

  const RoundTripResult roundtrip = perform_roundtrip(envelope, options_);
  if (roundtrip.issue.has_value()) {
    return result_from_issue<data::TuiOpenSessionResult>(*roundtrip.issue);
  }

  if (!roundtrip.response->ok()) {
    return result_from_issue<data::TuiOpenSessionResult>(
        make_issue_from_response(*roundtrip.response, envelope, options_.socket_path));
  }

  const auto* session = std::get_if<data::TuiSessionView>(&*roundtrip.response->payload);
  if (session == nullptr) {
    return result_from_issue<data::TuiOpenSessionResult>(make_protocol_issue(
        "malformed_response",
        "open_session expected a session payload",
        envelope,
        options_.socket_path));
  }

  return data::TuiOpenSessionResult{.session = *session, .issue = std::nullopt};
}

data::TuiSubmitTurnResult TuiIpcController::submit_turn(
    const data::TuiSubmitTurnRequest& request) {
  const TuiIpcRequestEnvelope envelope =
      make_request_envelope(request,
                            resolve_deadline(TuiIpcOperation::SubmitTurn,
                                             options_.timeout_policy));
  if (const auto validation_issue =
          validate_request(request, options_.socket_path, envelope);
      validation_issue.has_value()) {
    return result_from_issue<data::TuiSubmitTurnResult>(*validation_issue);
  }

  const RoundTripResult roundtrip = perform_roundtrip(envelope, options_);
  if (roundtrip.issue.has_value()) {
    return result_from_issue<data::TuiSubmitTurnResult>(*roundtrip.issue);
  }

  if (!roundtrip.response->ok()) {
    return result_from_issue<data::TuiSubmitTurnResult>(
        make_issue_from_response(*roundtrip.response, envelope, options_.socket_path));
  }

  const auto* receipt = std::get_if<data::TuiTurnReceipt>(&*roundtrip.response->payload);
  if (receipt == nullptr) {
    return result_from_issue<data::TuiSubmitTurnResult>(make_protocol_issue(
        "malformed_response",
        "submit_turn expected a receipt payload",
        envelope,
        options_.socket_path));
  }

  return data::TuiSubmitTurnResult{.receipt = *receipt, .issue = std::nullopt};
}

data::TuiPollEventsResult TuiIpcController::poll_events(
    const data::TuiPollEventsRequest& request) {
  const TuiIpcRequestEnvelope envelope =
      make_request_envelope(request,
                            resolve_deadline(TuiIpcOperation::PollEvents,
                                             options_.timeout_policy));
  if (const auto validation_issue =
          validate_request(request, options_.socket_path, envelope);
      validation_issue.has_value()) {
    return poll_result_from_issue(*validation_issue);
  }

  const RoundTripResult roundtrip = perform_roundtrip(envelope, options_);
  if (roundtrip.issue.has_value()) {
    return poll_result_from_issue(*roundtrip.issue);
  }

  if (!roundtrip.response->ok()) {
    return poll_result_from_issue(
        make_issue_from_response(*roundtrip.response, envelope, options_.socket_path));
  }

  const auto* batch = std::get_if<TuiIpcPollEventsBatch>(&*roundtrip.response->payload);
  if (batch == nullptr) {
    return poll_result_from_issue(make_protocol_issue(
        "malformed_response",
        "poll_events expected an events batch payload",
        envelope,
        options_.socket_path));
  }

  return data::TuiPollEventsResult{
      .events = batch->events,
      .next_cursor = batch->next_cursor,
      .issue = std::nullopt,
  };
}

data::TuiRouteCatalogResult TuiIpcController::query_route_catalog(
    const data::TuiRouteCatalogRequest& request) {
  const TuiIpcRequestEnvelope envelope =
      make_request_envelope(request,
                            resolve_deadline(TuiIpcOperation::RouteCatalog,
                                             options_.timeout_policy));
  if (const auto validation_issue =
          validate_request(request, options_.socket_path, envelope);
      validation_issue.has_value()) {
    return result_from_issue<data::TuiRouteCatalogResult>(*validation_issue);
  }

  const RoundTripResult roundtrip = perform_roundtrip(envelope, options_);
  if (roundtrip.issue.has_value()) {
    return result_from_issue<data::TuiRouteCatalogResult>(*roundtrip.issue);
  }

  if (!roundtrip.response->ok()) {
    return result_from_issue<data::TuiRouteCatalogResult>(
        make_issue_from_response(*roundtrip.response, envelope, options_.socket_path));
  }

  const auto* route_catalog =
      std::get_if<data::TuiRouteCatalogView>(&*roundtrip.response->payload);
  if (route_catalog == nullptr) {
    return result_from_issue<data::TuiRouteCatalogResult>(make_protocol_issue(
        "malformed_response",
        "query_route_catalog expected a route catalog payload",
        envelope,
        options_.socket_path));
  }

  return data::TuiRouteCatalogResult{
      .route_catalog = *route_catalog,
      .issue = std::nullopt,
  };
}

data::TuiCloseSessionResult TuiIpcController::close_session(
    const data::TuiCloseSessionRequest& request) {
  const TuiIpcRequestEnvelope envelope =
      make_request_envelope(request,
                            resolve_deadline(TuiIpcOperation::CloseSession,
                                             options_.timeout_policy));
  if (const auto validation_issue =
          validate_request(request, options_.socket_path, envelope);
      validation_issue.has_value()) {
    return result_from_issue<data::TuiCloseSessionResult>(*validation_issue);
  }

  const RoundTripResult roundtrip = perform_roundtrip(envelope, options_);
  if (roundtrip.issue.has_value()) {
    return result_from_issue<data::TuiCloseSessionResult>(*roundtrip.issue);
  }

  if (!roundtrip.response->ok()) {
    return result_from_issue<data::TuiCloseSessionResult>(
        make_issue_from_response(*roundtrip.response, envelope, options_.socket_path));
  }

  const auto* close_ack =
      std::get_if<TuiIpcCloseSessionAck>(&*roundtrip.response->payload);
  if (close_ack == nullptr || !close_ack->closed) {
    return result_from_issue<data::TuiCloseSessionResult>(make_protocol_issue(
        "malformed_response",
        "close_session expected a positive close acknowledgement",
        envelope,
        options_.socket_path));
  }

  return data::TuiCloseSessionResult{.closed = true, .issue = std::nullopt};
}

namespace test {

ScopedIpcOverride::ScopedIpcOverride(
    std::shared_ptr<dasall::platform::IIPC> ipc) {
  std::lock_guard<std::mutex> lock(g_test_ipc_mutex);
  previous_ = g_test_ipc_override;
  g_test_ipc_override = std::move(ipc);
}

ScopedIpcOverride::~ScopedIpcOverride() {
  std::lock_guard<std::mutex> lock(g_test_ipc_mutex);
  g_test_ipc_override = std::move(previous_);
}

std::string encode_response_envelope_for_test(
    const TuiIpcResponseEnvelope& envelope) {
  return encode_response_envelope(envelope);
}

std::optional<TuiIpcRequestEnvelope> decode_request_envelope_for_test(
    std::string_view payload) {
  return decode_request_envelope(payload);
}

}  // namespace test

}  // namespace dasall::tui::ipc
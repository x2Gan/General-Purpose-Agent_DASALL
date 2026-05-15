#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dasall::cognition::validation {

enum class JsonTokenKind : std::uint8_t {
  String = 0,
  Number,
  Object,
  Array,
  Boolean,
  Null,
  Unknown,
};

struct StructuredPayloadToken {
  std::string raw;
  JsonTokenKind kind = JsonTokenKind::Unknown;
};

class StructuredPayloadView;

class StructuredPayloadArrayView {
 public:
  StructuredPayloadArrayView() = default;
  explicit StructuredPayloadArrayView(std::vector<StructuredPayloadToken> items)
      : items_(std::move(items)) {}

  [[nodiscard]] std::size_t size() const { return items_.size(); }
  [[nodiscard]] bool empty() const { return items_.empty(); }
  [[nodiscard]] const std::vector<StructuredPayloadToken>& items() const { return items_; }
  [[nodiscard]] const StructuredPayloadToken* token_at(std::size_t index) const {
    return index < items_.size() ? &items_[index] : nullptr;
  }

  [[nodiscard]] std::optional<std::string> read_string(std::size_t index) const;
  [[nodiscard]] std::optional<double> read_number(std::size_t index) const;
  [[nodiscard]] std::optional<bool> read_bool(std::size_t index) const;
  [[nodiscard]] std::optional<StructuredPayloadArrayView> read_list(std::size_t index) const;
  [[nodiscard]] std::optional<StructuredPayloadView> read_object(std::size_t index) const;

 private:
  std::vector<StructuredPayloadToken> items_;
};

namespace detail {

using JsonObjectFields = std::unordered_map<std::string, StructuredPayloadToken>;

[[nodiscard]] inline std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }

  return value;
}

[[nodiscard]] inline std::string_view trim_view(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.remove_prefix(1U);
  }

  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.remove_suffix(1U);
  }

  return value;
}

inline void skip_ascii_ws(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
}

[[nodiscard]] inline bool consume_json_string(std::string_view text, std::size_t& cursor) {
  if (cursor >= text.size() || text[cursor] != '"') {
    return false;
  }

  ++cursor;
  bool escaped = false;
  while (cursor < text.size()) {
    const char current = text[cursor++];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (current == '\\') {
      escaped = true;
      continue;
    }
    if (current == '"') {
      return true;
    }
  }

  return false;
}

[[nodiscard]] inline std::optional<std::string> parse_json_string(std::string_view raw_token) {
  const auto trimmed = trim_view(raw_token);
  if (trimmed.size() < 2U || trimmed.front() != '"' || trimmed.back() != '"') {
    return std::nullopt;
  }

  std::string value;
  bool escaped = false;
  for (std::size_t index = 1U; index + 1U < trimmed.size(); ++index) {
    const char current = trimmed[index];
    if (escaped) {
      switch (current) {
        case '"':
        case '\\':
        case '/':
          value.push_back(current);
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
          value.push_back(current);
          break;
      }
      escaped = false;
      continue;
    }

    if (current == '\\') {
      escaped = true;
      continue;
    }

    value.push_back(current);
  }

  if (escaped) {
    return std::nullopt;
  }

  return value;
}

[[nodiscard]] inline std::optional<double> parse_json_number(std::string_view raw_token) {
  const auto trimmed = trim_copy(std::string(raw_token));
  if (trimmed.empty()) {
    return std::nullopt;
  }

  try {
    std::size_t parsed_length = 0U;
    const auto number = std::stod(trimmed, &parsed_length);
    if (parsed_length != trimmed.size()) {
      return std::nullopt;
    }
    return number;
  } catch (...) {
    return std::nullopt;
  }
}

[[nodiscard]] inline std::optional<bool> parse_json_bool(std::string_view raw_token) {
  const auto trimmed = trim_view(raw_token);
  if (trimmed == "true") {
    return true;
  }
  if (trimmed == "false") {
    return false;
  }
  return std::nullopt;
}

[[nodiscard]] inline JsonTokenKind classify_json_token(std::string_view raw_token) {
  const auto trimmed = trim_view(raw_token);
  if (trimmed.empty()) {
    return JsonTokenKind::Unknown;
  }

  switch (trimmed.front()) {
    case '"':
      return JsonTokenKind::String;
    case '{':
      return JsonTokenKind::Object;
    case '[':
      return JsonTokenKind::Array;
    case 't':
    case 'f':
      return JsonTokenKind::Boolean;
    case 'n':
      return JsonTokenKind::Null;
    default:
      if (trimmed.front() == '-' || std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0) {
        return JsonTokenKind::Number;
      }
      return JsonTokenKind::Unknown;
  }
}

[[nodiscard]] inline std::optional<StructuredPayloadToken> parse_json_value_token(
    std::string_view text,
    std::size_t& cursor) {
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size()) {
    return std::nullopt;
  }

  const auto token_begin = cursor;
  if (text[cursor] == '"') {
    if (!consume_json_string(text, cursor)) {
      return std::nullopt;
    }
    return StructuredPayloadToken{
        .raw = std::string(text.substr(token_begin, cursor - token_begin)),
        .kind = JsonTokenKind::String,
    };
  }

  int nested_objects = 0;
  int nested_arrays = 0;
  bool in_string = false;
  bool escaped = false;
  while (cursor < text.size()) {
    const char current = text[cursor];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (current == '\\') {
        escaped = true;
      } else if (current == '"') {
        in_string = false;
      }
      ++cursor;
      continue;
    }

    switch (current) {
      case '"':
        in_string = true;
        ++cursor;
        continue;
      case '{':
        ++nested_objects;
        ++cursor;
        continue;
      case '}':
        if (nested_objects == 0 && nested_arrays == 0) {
          goto value_done;
        }
        --nested_objects;
        ++cursor;
        continue;
      case '[':
        ++nested_arrays;
        ++cursor;
        continue;
      case ']':
        if (nested_arrays == 0) {
          goto value_done;
        }
        --nested_arrays;
        ++cursor;
        continue;
      case ',':
        if (nested_objects == 0 && nested_arrays == 0) {
          goto value_done;
        }
        ++cursor;
        continue;
      default:
        ++cursor;
        continue;
    }
  }

value_done:
  if (in_string || nested_objects != 0 || nested_arrays != 0) {
    return std::nullopt;
  }

  const auto raw_token = trim_copy(std::string(text.substr(token_begin, cursor - token_begin)));
  if (raw_token.empty()) {
    return std::nullopt;
  }

  return StructuredPayloadToken{
      .raw = raw_token,
      .kind = classify_json_token(raw_token),
  };
}

[[nodiscard]] inline std::optional<JsonObjectFields> parse_json_object_fields(std::string_view text) {
  std::size_t cursor = 0U;
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size() || text[cursor] != '{') {
    return std::nullopt;
  }
  ++cursor;

  JsonObjectFields fields;
  while (cursor < text.size()) {
    skip_ascii_ws(text, cursor);
    if (cursor >= text.size()) {
      return std::nullopt;
    }
    if (text[cursor] == '}') {
      ++cursor;
      skip_ascii_ws(text, cursor);
      return cursor == text.size() ? std::optional<JsonObjectFields>(std::move(fields))
                                   : std::nullopt;
    }

    const auto key_begin = cursor;
    if (!consume_json_string(text, cursor)) {
      return std::nullopt;
    }
    const auto key = parse_json_string(text.substr(key_begin, cursor - key_begin));
    if (!key.has_value()) {
      return std::nullopt;
    }

    skip_ascii_ws(text, cursor);
    if (cursor >= text.size() || text[cursor] != ':') {
      return std::nullopt;
    }
    ++cursor;

    const auto value = parse_json_value_token(text, cursor);
    if (!value.has_value()) {
      return std::nullopt;
    }
    fields[*key] = *value;

    skip_ascii_ws(text, cursor);
    if (cursor >= text.size()) {
      return std::nullopt;
    }
    if (text[cursor] == ',') {
      ++cursor;
      continue;
    }
    if (text[cursor] == '}') {
      ++cursor;
      skip_ascii_ws(text, cursor);
      return cursor == text.size() ? std::optional<JsonObjectFields>(std::move(fields))
                                   : std::nullopt;
    }
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] inline std::optional<std::vector<StructuredPayloadToken>> parse_json_array_items(
    std::string_view raw_token) {
  const auto trimmed = trim_view(raw_token);
  if (trimmed.size() < 2U || trimmed.front() != '[' || trimmed.back() != ']') {
    return std::nullopt;
  }

  std::vector<StructuredPayloadToken> items;
  std::size_t cursor = 1U;
  while (cursor + 1U <= trimmed.size()) {
    skip_ascii_ws(trimmed, cursor);
    if (cursor >= trimmed.size() - 1U) {
      return items;
    }

    const auto item = parse_json_value_token(trimmed, cursor);
    if (!item.has_value()) {
      return std::nullopt;
    }
    items.push_back(*item);

    skip_ascii_ws(trimmed, cursor);
    if (cursor >= trimmed.size() - 1U) {
      return items;
    }
    if (trimmed[cursor] != ',') {
      return std::nullopt;
    }
    ++cursor;
  }

  return items;
}

}  // namespace detail

class StructuredPayloadView {
 public:
  StructuredPayloadView() = default;

  [[nodiscard]] static std::optional<StructuredPayloadView> parse_structured_payload(
      std::string_view payload) {
    const auto fields = detail::parse_json_object_fields(payload);
    if (!fields.has_value()) {
      return std::nullopt;
    }
    return StructuredPayloadView(*fields);
  }

  [[nodiscard]] std::vector<std::string> field_names() const {
    std::vector<std::string> names;
    names.reserve(fields_.size());
    for (const auto& [field_name, field_token] : fields_) {
      static_cast<void>(field_token);
      names.push_back(field_name);
    }
    return names;
  }

  [[nodiscard]] bool has_field(std::string_view field_path) const {
    return field_token(field_path).has_value();
  }

  [[nodiscard]] std::optional<StructuredPayloadToken> field_token(std::string_view field_path) const {
    if (field_path.empty()) {
      return std::nullopt;
    }

    const auto separator = field_path.find('.');
    const auto segment = separator == std::string_view::npos
                             ? field_path
                             : field_path.substr(0U, separator);
    const auto field_it = fields_.find(std::string(segment));
    if (field_it == fields_.end()) {
      return std::nullopt;
    }

    if (separator == std::string_view::npos) {
      return field_it->second;
    }
    if (field_it->second.kind != JsonTokenKind::Object) {
      return std::nullopt;
    }

    const auto nested_view = parse_structured_payload(field_it->second.raw);
    if (!nested_view.has_value()) {
      return std::nullopt;
    }
    return nested_view->field_token(field_path.substr(separator + 1U));
  }

  [[nodiscard]] std::optional<std::string> read_string(std::string_view field_path) const {
    const auto token = field_token(field_path);
    if (!token.has_value() || token->kind != JsonTokenKind::String) {
      return std::nullopt;
    }
    return detail::parse_json_string(token->raw);
  }

  [[nodiscard]] std::optional<double> read_number(std::string_view field_path) const {
    const auto token = field_token(field_path);
    if (!token.has_value() || token->kind != JsonTokenKind::Number) {
      return std::nullopt;
    }
    return detail::parse_json_number(token->raw);
  }

  [[nodiscard]] std::optional<bool> read_bool(std::string_view field_path) const {
    const auto token = field_token(field_path);
    if (!token.has_value() || token->kind != JsonTokenKind::Boolean) {
      return std::nullopt;
    }
    return detail::parse_json_bool(token->raw);
  }

  [[nodiscard]] std::optional<StructuredPayloadArrayView> read_list(
      std::string_view field_path) const {
    const auto token = field_token(field_path);
    if (!token.has_value() || token->kind != JsonTokenKind::Array) {
      return std::nullopt;
    }

    const auto items = detail::parse_json_array_items(token->raw);
    if (!items.has_value()) {
      return std::nullopt;
    }
    return StructuredPayloadArrayView(*items);
  }

  [[nodiscard]] std::optional<StructuredPayloadView> read_object(
      std::string_view field_path) const {
    if (field_path.empty()) {
      return *this;
    }

    const auto token = field_token(field_path);
    if (!token.has_value() || token->kind != JsonTokenKind::Object) {
      return std::nullopt;
    }
    return parse_structured_payload(token->raw);
  }

 private:
  explicit StructuredPayloadView(detail::JsonObjectFields fields) : fields_(std::move(fields)) {}

  detail::JsonObjectFields fields_;
};

[[nodiscard]] inline std::optional<std::string> StructuredPayloadArrayView::read_string(
    std::size_t index) const {
  const auto* token = token_at(index);
  if (token == nullptr || token->kind != JsonTokenKind::String) {
    return std::nullopt;
  }
  return detail::parse_json_string(token->raw);
}

[[nodiscard]] inline std::optional<double> StructuredPayloadArrayView::read_number(
    std::size_t index) const {
  const auto* token = token_at(index);
  if (token == nullptr || token->kind != JsonTokenKind::Number) {
    return std::nullopt;
  }
  return detail::parse_json_number(token->raw);
}

[[nodiscard]] inline std::optional<bool> StructuredPayloadArrayView::read_bool(
    std::size_t index) const {
  const auto* token = token_at(index);
  if (token == nullptr || token->kind != JsonTokenKind::Boolean) {
    return std::nullopt;
  }
  return detail::parse_json_bool(token->raw);
}

[[nodiscard]] inline std::optional<StructuredPayloadArrayView> StructuredPayloadArrayView::read_list(
    std::size_t index) const {
  const auto* token = token_at(index);
  if (token == nullptr || token->kind != JsonTokenKind::Array) {
    return std::nullopt;
  }

  const auto items = detail::parse_json_array_items(token->raw);
  if (!items.has_value()) {
    return std::nullopt;
  }
  return StructuredPayloadArrayView(*items);
}

[[nodiscard]] inline std::optional<StructuredPayloadView> StructuredPayloadArrayView::read_object(
    std::size_t index) const {
  const auto* token = token_at(index);
  if (token == nullptr || token->kind != JsonTokenKind::Object) {
    return std::nullopt;
  }
  return StructuredPayloadView::parse_structured_payload(token->raw);
}

}  // namespace dasall::cognition::validation

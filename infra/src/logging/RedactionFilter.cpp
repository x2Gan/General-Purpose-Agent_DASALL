#include "logging/RedactionFilter.h"

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace dasall::infra::logging {

namespace {

constexpr std::array<std::string_view, 11> kSensitiveValuePrefixes = {
    "bearer ",
    "token=",
    "token:",
    "secret=",
    "secret:",
    "password=",
    "password:",
    "authorization=",
    "authorization:",
    "api_key=",
    "apikey=",
};

[[nodiscard]] bool is_value_delimiter(char ch) {
  switch (ch) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case ',':
    case ';':
    case '&':
    case ')':
    case '(': 
    case ']':
    case '[':
    case '}':
    case '{':
    case '"':
    case '\'':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::string lower_copy(std::string_view text) {
  std::string lowered(text);
  for (auto& ch : lowered) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return lowered;
}

void redact_prefix_payload(std::string& text, std::string_view prefix) {
  constexpr std::string_view kAuthorizationPrefix = "authorization:";
  constexpr std::string_view kAuthorizationEquals = "authorization=";
  constexpr std::string_view kBearerPrefix = "bearer ";

  std::size_t search_pos = 0;
  while (search_pos < text.size()) {
    const auto lowered = lower_copy(text);
    const auto match_pos = lowered.find(prefix, search_pos);
    if (match_pos == std::string::npos) {
      break;
    }

    auto value_start = match_pos + prefix.size();
    while (value_start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[value_start])) != 0) {
      ++value_start;
    }

    if ((prefix == kAuthorizationPrefix || prefix == kAuthorizationEquals) &&
        value_start < text.size()) {
      const auto authorization_value =
          lower_copy(std::string_view(text).substr(value_start));
      if (authorization_value.rfind(kBearerPrefix, 0) == 0) {
        value_start += kBearerPrefix.size();
      }
    }

    auto value_end = value_start;
    while (value_end < text.size() && !is_value_delimiter(text[value_end])) {
      ++value_end;
    }

    if (value_end == value_start) {
      search_pos = value_start + 1;
      continue;
    }

    text.replace(value_start,
                 value_end - value_start,
                 std::string(LogEvent::kRedactedValue));
    search_pos = value_start + LogEvent::kRedactedValue.size();
  }
}

[[nodiscard]] std::string redact_text(std::string value) {
  for (const auto prefix : kSensitiveValuePrefixes) {
    redact_prefix_payload(value, prefix);
  }

  return value;
}

}  // namespace

LogEvent RedactionFilter::apply(const LogEvent& event) const {
  auto filtered = event;
  filtered.message = redact_text(filtered.message);
  filtered.attrs = event.redacted_attrs();

  for (auto& [key, value] : filtered.attrs) {
    if (LogEvent::is_sensitive_attr_key(key)) {
      value = std::string(LogEvent::kRedactedValue);
      continue;
    }

    value = redact_text(value);
  }

  return filtered;
}

}  // namespace dasall::infra::logging
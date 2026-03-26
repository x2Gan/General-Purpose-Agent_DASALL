#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace dasall::infra {

enum class LogLevel {
  Unspecified = 0,
  Trace = 1,
  Debug = 2,
  Info = 3,
  Warn = 4,
  Error = 5,
  Fatal = 6,
};

struct LogEvent {
  using AttributeMap = std::map<std::string, std::string>;

  static constexpr std::string_view kRedactedValue = "<redacted>";

  LogLevel level = LogLevel::Unspecified;
  std::string module;
  std::string message;
  AttributeMap attrs;
  std::optional<std::int64_t> ts;

  [[nodiscard]] const std::string& category() const {
    return module;
  }

  [[nodiscard]] bool attrs_are_serializable() const {
    return std::all_of(attrs.begin(), attrs.end(), [](const auto& entry) {
      return !entry.first.empty();
    });
  }

  [[nodiscard]] bool has_timestamp() const {
    return ts.has_value() && *ts >= 0;
  }

  [[nodiscard]] static bool is_sensitive_attr_key(std::string_view key) {
    static constexpr std::array<std::string_view, 6> kSensitiveKeyFragments = {
        "token",
        "secret",
        "password",
        "authorization",
        "api_key",
        "apikey",
    };

    for (const auto fragment : kSensitiveKeyFragments) {
      if (key.find(fragment) != std::string_view::npos) {
        return true;
      }
    }

    return false;
  }

  [[nodiscard]] AttributeMap redacted_attrs() const {
    auto result = attrs;
    for (auto& [key, value] : result) {
      if (is_sensitive_attr_key(key)) {
        value = std::string(kRedactedValue);
      }
    }
    return result;
  }
};

}  // namespace dasall::infra
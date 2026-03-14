#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>

namespace dasall::contracts {

struct TimeoutFieldSet {
  std::optional<std::int64_t> created_at_ms;
  std::optional<std::int64_t> deadline_at_ms;
  std::optional<std::uint32_t> timeout_ms;
  std::optional<std::uint32_t> timeout_seconds;
};

struct TimeoutNormalizationResult {
  bool ok = false;
  std::optional<std::int64_t> normalized_deadline_at_ms;
  std::optional<std::uint32_t> normalized_timeout_ms;
  bool used_legacy_timeout_seconds = false;
  std::string reason;
};

// Applies the T010 rule set: milliseconds are canonical, deadline_at is authoritative,
// and legacy timeout_seconds is read-only compatibility input.
inline TimeoutNormalizationResult normalize_timeout_fields(const TimeoutFieldSet& fields) {
  TimeoutNormalizationResult result;

  std::optional<std::uint32_t> effective_timeout_ms = fields.timeout_ms;
  if (fields.timeout_seconds.has_value()) {
    const auto migrated_timeout_ms = static_cast<std::uint32_t>(*fields.timeout_seconds * 1000U);
    if (fields.timeout_ms.has_value() && *fields.timeout_ms != migrated_timeout_ms) {
      result.reason = "timeout_seconds and timeout_ms are inconsistent";
      return result;
    }

    effective_timeout_ms = migrated_timeout_ms;
    result.used_legacy_timeout_seconds = true;
  }

  if (fields.deadline_at_ms.has_value()) {
    result.ok = true;
    result.normalized_deadline_at_ms = fields.deadline_at_ms;
    result.normalized_timeout_ms = effective_timeout_ms;
    return result;
  }

  if (!effective_timeout_ms.has_value()) {
    result.ok = true;
    return result;
  }

  if (!fields.created_at_ms.has_value()) {
    result.reason = "created_at_ms is required when only timeout fields are provided";
    return result;
  }

  result.ok = true;
  result.normalized_timeout_ms = effective_timeout_ms;
  result.normalized_deadline_at_ms = *fields.created_at_ms + *effective_timeout_ms;
  return result;
}

inline bool is_known_enum_value(int raw_value, std::span<const int> known_values) {
  for (const int known_value : known_values) {
    if (raw_value == known_value) {
      return true;
    }
  }

  return false;
}

template <typename Enum>
inline Enum fallback_unknown_enum_value(int raw_value,
                                        std::span<const int> known_values,
                                        Enum unspecified_value) {
  if (is_known_enum_value(raw_value, known_values)) {
    return static_cast<Enum>(raw_value);
  }

  return unspecified_value;
}

}  // namespace dasall::contracts
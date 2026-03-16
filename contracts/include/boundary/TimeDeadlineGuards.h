#pragma once

#include <cstdint>
#include <string_view>

#include "boundary/CompatibilityGuards.h"

namespace dasall::contracts {

struct TimeDeadlineGuardResult {
  bool ok = false;
  bool used_deadline_priority = false;
  bool used_legacy_timeout_seconds = false;
  std::string_view reason = "time deadline validation failed";
};

// Validates T010 + T014 D6 frozen semantics on top of timeout normalization:
// 1) deadline_at_ms remains authoritative once present.
// 2) timeout_seconds is read-only compatibility input via timeout_ms migration.
// 3) if created_at_ms + normalized_timeout_ms can be derived together with
//    deadline_at_ms, the values must be consistent; otherwise fail fast.
inline TimeDeadlineGuardResult validate_time_deadline_fields(const TimeoutFieldSet& fields) {
  const auto normalized = normalize_timeout_fields(fields);
  if (!normalized.ok) {
    return TimeDeadlineGuardResult{
        .ok = false,
        .used_deadline_priority = normalized.used_deadline_priority,
        .used_legacy_timeout_seconds = normalized.used_legacy_timeout_seconds,
        .reason = "timeout field normalization failed",
    };
  }

  if (fields.deadline_at_ms.has_value() && fields.created_at_ms.has_value() &&
      normalized.normalized_timeout_ms.has_value()) {
    const auto expected_deadline = *fields.created_at_ms +
                                   static_cast<std::int64_t>(*normalized.normalized_timeout_ms);
    if (*fields.deadline_at_ms != expected_deadline) {
      return TimeDeadlineGuardResult{
          .ok = false,
          .used_deadline_priority = normalized.used_deadline_priority,
          .used_legacy_timeout_seconds = normalized.used_legacy_timeout_seconds,
          .reason = "deadline_at_ms conflicts with created_at_ms plus timeout_ms",
      };
    }
  }

  return TimeDeadlineGuardResult{
      .ok = true,
      .used_deadline_priority = normalized.used_deadline_priority,
      .used_legacy_timeout_seconds = normalized.used_legacy_timeout_seconds,
      .reason = "time deadline fields are valid",
  };
}

}  // namespace dasall::contracts

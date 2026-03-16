#pragma once

#include <array>
#include <string_view>

#include "boundary/GuardCommon.h"
#include "event/EventEnvelope.h"

namespace dasall::contracts {

struct EventEnvelopeGuardResult {
  bool ok = false;
  std::string_view reason = "event envelope validation failed";
};

inline bool is_event_header_key_whitelisted(std::string_view key) {
  constexpr std::array<std::string_view, 6> kHeaderWhitelist{
      "event_id",
      "event_type",
      "event_version",
      "occurred_at_ms",
      "request_id",
      "trace_id",
  };

  for (const auto allowed : kHeaderWhitelist) {
    if (allowed == key) {
      return true;
    }
  }

  return false;
}

// Validates WP02-T011 + T014 D7 frozen constraints:
// 1) header contains only cross-cutting common fields.
// 2) module-private fields must stay in payload and cannot appear in header.
// 3) required common metadata and payload carrier must be present.
inline EventEnvelopeGuardResult validate_event_envelope(const EventEnvelope& envelope) {
  if (!has_non_empty_value(envelope.header.event_id)) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.event_id is required"};
  }

  if (!has_non_empty_value(envelope.header.event_type)) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.event_type is required"};
  }

  if (!envelope.header.event_version.has_value()) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.event_version is required"};
  }

  if (*envelope.header.event_version == 0U) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.event_version must be greater than zero"};
  }

  if (!envelope.header.occurred_at_ms.has_value()) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.occurred_at_ms is required"};
  }

  if (!has_non_empty_value(envelope.header.request_id)) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.request_id is required"};
  }

  if (!has_non_empty_value(envelope.header.trace_id)) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "header.trace_id is required"};
  }

  if (!has_non_empty_value(envelope.payload_type)) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "payload_type is required"};
  }

  if (!has_non_empty_value(envelope.payload_json)) {
    return EventEnvelopeGuardResult{.ok = false, .reason = "payload_json is required"};
  }

  for (const auto& key : envelope.header.header_keys) {
    if (!is_event_header_key_whitelisted(key)) {
      return EventEnvelopeGuardResult{.ok = false,
                                      .reason = "header contains non-whitelisted key"};
    }
  }

  return EventEnvelopeGuardResult{.ok = true, .reason = "event envelope is valid"};
}

}  // namespace dasall::contracts

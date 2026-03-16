#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// EventEnvelopeHeader freezes the WP02-T011 common header surface.
// The header is restricted to cross-cutting common metadata and must not carry
// module-private semantics.
struct EventEnvelopeHeader {
  std::optional<std::string> event_id;
  std::optional<std::string> event_type;
  std::optional<std::uint32_t> event_version;
  std::optional<std::int64_t> occurred_at_ms;
  std::optional<std::string> request_id;
  std::optional<std::string> trace_id;

  // Header keys observed from serialized transport representation.
  // Guards use this list to enforce the whitelist and reject private fields
  // that are incorrectly promoted to the header.
  std::vector<std::string> header_keys;
};

// EventEnvelope keeps private/module-specific details in payload only.
struct EventEnvelope {
  EventEnvelopeHeader header;
  std::optional<std::string> payload_type;
  std::optional<std::string> payload_json;
};

}  // namespace dasall::contracts

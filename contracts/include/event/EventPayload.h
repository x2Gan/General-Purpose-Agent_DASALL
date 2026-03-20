#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::contracts {

// EventPayload freezes the payload-only surface under EventEnvelope.
// It intentionally excludes envelope-header metadata and transport keys.
struct EventPayload {
  // Payload semantic type anchor, e.g. "checkpoint_budget_snapshot".
  std::optional<std::string> payload_type;

  // Serialized payload representation used by cross-module boundaries.
  std::optional<std::string> payload_json;

  // Optional schema reference for compatibility audits.
  std::optional<std::string> schema_ref;

  // Observed payload field aliases from producer serialization output.
  // Guards validate this list to prevent envelope-header alias leakage.
  std::optional<std::vector<std::string>> field_aliases;

  // Producer module hint for diagnostics and traceability.
  std::optional<std::string> producer_module;

  // Optional payload version maintained by payload owners.
  std::optional<std::uint32_t> payload_version;
};

}  // namespace dasall::contracts

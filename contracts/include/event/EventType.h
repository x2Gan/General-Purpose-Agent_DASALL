#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dasall::contracts {

// EventDomain normalizes the event ownership domain for routing and governance.
// Unspecified is the required enum sentinel for forward-compatible decoding.
enum class EventDomain {
  Unspecified = 0,
  Checkpoint = 1,
  Tool = 2,
  Recovery = 3,
  MultiAgent = 4,
  Memory = 5,
};

// EventStabilityTier marks the rollout confidence level of one event type.
enum class EventStabilityTier {
  Unspecified = 0,
  Stable = 1,
  Preview = 2,
  Internal = 3,
};

// EventType freezes the stable "event-type identity" contract surface.
// This object models type identity only and does not carry payload data.
struct EventType {
  // Canonical event type key, e.g. "checkpoint.snapshot.created".
  std::optional<std::string> type_key;

  // Domain ownership of this event type.
  std::optional<EventDomain> domain;

  // Major compatibility version consumed by envelope/event routers.
  std::optional<std::uint32_t> major_version;

  // Optional schema revision for payload evolution tracking.
  std::optional<std::uint32_t> schema_revision;

  // Release-state hint for admission and rollout checks.
  std::optional<EventStabilityTier> stability_tier;
};

// Stable lookup table for diagnostics and test assertions.
inline constexpr std::array<std::string_view, 6> kEventDomainNames = {
    "Unspecified",
    "Checkpoint",
    "Tool",
    "Recovery",
    "MultiAgent",
    "Memory",
};

}  // namespace dasall::contracts

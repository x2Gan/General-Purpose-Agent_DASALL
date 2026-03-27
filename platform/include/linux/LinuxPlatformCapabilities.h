#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace dasall::platform::linux {

enum class PlatformCapabilityState {
  Enabled,
  Disabled,
  Degraded,
};

struct PlatformCapability {
  static constexpr std::string_view kReasonNotProbed = "NotProbed";

  PlatformCapabilityState state = PlatformCapabilityState::Disabled;
  std::string reason = std::string(kReasonNotProbed);

  [[nodiscard]] static PlatformCapability enabled() {
    return PlatformCapability{.state = PlatformCapabilityState::Enabled, .reason = {}};
  }

  [[nodiscard]] static PlatformCapability disabled(std::string reason_text) {
    return PlatformCapability{
        .state = PlatformCapabilityState::Disabled,
        .reason = std::move(reason_text),
    };
  }

  [[nodiscard]] static PlatformCapability degraded(std::string reason_text) {
    return PlatformCapability{
        .state = PlatformCapabilityState::Degraded,
        .reason = std::move(reason_text),
    };
  }

  [[nodiscard]] bool is_enabled() const {
    return state == PlatformCapabilityState::Enabled;
  }

  [[nodiscard]] bool is_disabled() const {
    return state == PlatformCapabilityState::Disabled;
  }

  [[nodiscard]] bool is_degraded() const {
    return state == PlatformCapabilityState::Degraded;
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (is_enabled()) {
      return reason.empty();
    }

    return !reason.empty();
  }
};

struct PlatformCapabilitySet {
  PlatformCapability thread;
  PlatformCapability timer;
  PlatformCapability queue;
  PlatformCapability filesystem;
  PlatformCapability network;
  PlatformCapability ipc;
  PlatformCapability hal;

  [[nodiscard]] bool has_consistent_values() const {
    return thread.has_consistent_values() && timer.has_consistent_values() &&
           queue.has_consistent_values() && filesystem.has_consistent_values() &&
           network.has_consistent_values() && ipc.has_consistent_values() &&
           hal.has_consistent_values();
  }

  [[nodiscard]] std::size_t degraded_count() const {
    return static_cast<std::size_t>(thread.is_degraded()) +
           static_cast<std::size_t>(timer.is_degraded()) +
           static_cast<std::size_t>(queue.is_degraded()) +
           static_cast<std::size_t>(filesystem.is_degraded()) +
           static_cast<std::size_t>(network.is_degraded()) +
           static_cast<std::size_t>(ipc.is_degraded()) +
           static_cast<std::size_t>(hal.is_degraded());
  }
};

}  // namespace dasall::platform::linux
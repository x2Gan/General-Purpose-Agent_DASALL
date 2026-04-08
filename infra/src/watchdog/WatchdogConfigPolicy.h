#pragma once

#include <optional>

#include "watchdog/IWatchdogService.h"

namespace dasall::infra::watchdog {

struct WatchdogConfigPatch {
  std::optional<bool> enabled;
  std::optional<std::uint32_t> scan_interval_ms;
  std::optional<std::uint32_t> timeout_ms;
  std::optional<std::uint32_t> grace_ms;
  std::optional<std::uint32_t> consecutive_miss_threshold;
  std::optional<WatchdogTimeoutLevelPolicy> timeout_level_policy;
  std::optional<std::uint32_t> event_queue_size;
  std::optional<WatchdogEventOverflowPolicy> event_overflow_policy;
  std::optional<bool> recovery_hint_enabled;
  std::optional<bool> audit_required;
  std::optional<std::uint32_t> max_entities;
  std::optional<std::uint32_t> safe_mode_scan_interval_ms;
};

class WatchdogConfigPolicy {
 public:
  WatchdogConfigPolicy();
  explicit WatchdogConfigPolicy(WatchdogServiceConfig default_config);

  [[nodiscard]] const WatchdogServiceConfig& load_defaults() const;
  [[nodiscard]] WatchdogServiceConfig merge_layers(
      const WatchdogConfigPatch& profile,
      const WatchdogConfigPatch& deploy,
      const WatchdogConfigPatch& runtime) const;
  [[nodiscard]] WatchdogOperationResult validate_limits(
      const WatchdogServiceConfig& config) const;

 private:
  static void apply_patch(WatchdogServiceConfig& resolved,
                          const WatchdogConfigPatch& patch);

  WatchdogServiceConfig default_config_;
};

}  // namespace dasall::infra::watchdog
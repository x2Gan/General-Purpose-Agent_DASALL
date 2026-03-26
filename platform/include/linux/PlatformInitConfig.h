#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dasall::platform::linux {

struct QueueDefaults {
  static constexpr std::size_t kDefaultCapacity = 1024;
  static constexpr std::string_view kDefaultOverflowPolicy = "reject";

  std::size_t capacity = kDefaultCapacity;
  std::string overflow_policy = std::string(kDefaultOverflowPolicy);

  [[nodiscard]] bool is_valid() const {
    return capacity > 0 &&
           (overflow_policy == kDefaultOverflowPolicy || overflow_policy == "block");
  }
};

struct IoTimeouts {
  static constexpr std::int64_t kDefaultConnectTimeoutMs = 3000;
  static constexpr std::int64_t kDefaultIoTimeoutMs = 5000;

  std::int64_t connect_timeout_ms = kDefaultConnectTimeoutMs;
  std::int64_t io_timeout_ms = kDefaultIoTimeoutMs;

  [[nodiscard]] bool is_valid() const {
    return connect_timeout_ms >= 0 && io_timeout_ms >= 0;
  }
};

struct PlatformInitConfig {
  static constexpr std::string_view kDefaultTargetPlatform = "linux";
  static constexpr std::string_view kDefaultProfileName = "desktop_full";

  std::string target_platform = std::string(kDefaultTargetPlatform);
  std::string profile_name = std::string(kDefaultProfileName);
  bool enable_hal = false;
  QueueDefaults queue_defaults;
  IoTimeouts io_timeouts;

  [[nodiscard]] bool has_consistent_values() const {
    return !target_platform.empty() && !profile_name.empty() &&
           queue_defaults.is_valid() && io_timeouts.is_valid();
  }
};

}  // namespace dasall::platform::linux
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ota/OTATypes.h"

namespace dasall::infra::ota {

enum class OTAMode {
  DryRun = 0,
  ApplyEnabled = 1,
};

inline constexpr std::string_view ota_mode_name(OTAMode mode) {
  switch (mode) {
    case OTAMode::DryRun:
      return "dry_run";
    case OTAMode::ApplyEnabled:
      return "apply_enabled";
  }

  return "dry_run";
}

struct OTAHealthSnapshot {
  bool ready = false;
  bool degraded = false;
  std::vector<std::string> failed_components;

  [[nodiscard]] bool is_valid() const {
    return has_unique_non_empty_values(failed_components);
  }
};

struct OTAResourceSnapshot {
  std::uint64_t free_space_mb = 0;
  std::uint32_t cpu_load_pct = 0;

  [[nodiscard]] bool is_valid() const {
    return cpu_load_pct <= 100;
  }
};

struct OTAPrecheckPolicy {
  bool enabled = true;
  OTAMode mode = OTAMode::DryRun;
  std::uint64_t min_free_space_mb = 256;
  std::uint32_t max_cpu_load_pct = 80;
  bool require_health_ready = true;
  std::uint32_t consecutive_failures = 0;
  std::uint32_t freeze_after_failures = 3;

  [[nodiscard]] bool is_valid() const {
    return max_cpu_load_pct <= 100;
  }
};

class IOTAHealthSignalProvider {
 public:
  virtual ~IOTAHealthSignalProvider() = default;

  [[nodiscard]] virtual OTAHealthSnapshot current_health() const = 0;
};

class IOTAResourceProbe {
 public:
  virtual ~IOTAResourceProbe() = default;

  [[nodiscard]] virtual OTAResourceSnapshot current_resources() const = 0;
};

class IOTAPrecheckPolicyProvider {
 public:
  virtual ~IOTAPrecheckPolicyProvider() = default;

  [[nodiscard]] virtual OTAPrecheckPolicy current_policy() const = 0;
};

class OTAPrecheckService {
 public:
  struct Dependencies {
    const IOTAHealthSignalProvider* health_provider = nullptr;
    const IOTAResourceProbe* resource_probe = nullptr;
    const IOTAPrecheckPolicyProvider* policy_provider = nullptr;
  };

  explicit OTAPrecheckService(Dependencies dependencies);

  [[nodiscard]] PrecheckReport precheck(const UpgradePlan& plan) const;

 private:
  Dependencies dependencies_;
};

}  // namespace dasall::infra::ota
#pragma once

#include <string>
#include <string_view>

namespace dasall::infra::plugin {

inline constexpr std::string_view kPluginUnknownValue = "unknown";

enum class PluginTrustLevel {
  Unknown = 0,
  Untrusted = 1,
  External = 2,
  Vendor = 3,
  Internal = 4,
};

inline constexpr std::string_view plugin_trust_level_name(PluginTrustLevel trust_level) {
  switch (trust_level) {
    case PluginTrustLevel::Unknown:
      return "unknown";
    case PluginTrustLevel::Untrusted:
      return "untrusted";
    case PluginTrustLevel::External:
      return "external";
    case PluginTrustLevel::Vendor:
      return "vendor";
    case PluginTrustLevel::Internal:
      return "internal";
  }

  return "unknown";
}

enum class PluginStatus {
  Unknown = 0,
  Discovered = 1,
  Validated = 2,
  Rejected = 3,
  Loaded = 4,
  Active = 5,
  Disabled = 6,
  Unloaded = 7,
};

inline constexpr std::string_view plugin_status_name(PluginStatus status) {
  switch (status) {
    case PluginStatus::Unknown:
      return "unknown";
    case PluginStatus::Discovered:
      return "discovered";
    case PluginStatus::Validated:
      return "validated";
    case PluginStatus::Rejected:
      return "rejected";
    case PluginStatus::Loaded:
      return "loaded";
    case PluginStatus::Active:
      return "active";
    case PluginStatus::Disabled:
      return "disabled";
    case PluginStatus::Unloaded:
      return "unloaded";
  }

  return "unknown";
}

[[nodiscard]] inline std::string plugin_value_or_unknown(std::string_view value) {
  if (value.empty()) {
    return std::string(kPluginUnknownValue);
  }

  return std::string(value);
}

struct PluginDescriptor {
  std::string plugin_id = std::string(kPluginUnknownValue);
  std::string version = std::string(kPluginUnknownValue);
  std::string abi = std::string(kPluginUnknownValue);
  PluginTrustLevel trust_level = PluginTrustLevel::Unknown;
  PluginStatus status = PluginStatus::Unknown;
  std::string source = std::string(kPluginUnknownValue);

  [[nodiscard]] bool uses_unknown_defaults() const {
    return plugin_id == kPluginUnknownValue && version == kPluginUnknownValue &&
           abi == kPluginUnknownValue && trust_level == PluginTrustLevel::Unknown &&
           status == PluginStatus::Unknown && source == kPluginUnknownValue;
  }

  [[nodiscard]] bool is_governance_ready() const {
    return plugin_id != kPluginUnknownValue && version != kPluginUnknownValue &&
           abi != kPluginUnknownValue && trust_level != PluginTrustLevel::Unknown &&
           status != PluginStatus::Unknown && source != kPluginUnknownValue;
  }

  [[nodiscard]] static PluginDescriptor normalize(PluginDescriptor descriptor) {
    descriptor.plugin_id = plugin_value_or_unknown(descriptor.plugin_id);
    descriptor.version = plugin_value_or_unknown(descriptor.version);
    descriptor.abi = plugin_value_or_unknown(descriptor.abi);
    descriptor.source = plugin_value_or_unknown(descriptor.source);
    return descriptor;
  }
};

}  // namespace dasall::infra::plugin
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "plugin/PluginDescriptor.h"

namespace dasall::infra::plugin {

[[nodiscard]] inline bool is_plugin_manifest_slug_segment(std::string_view segment) {
  if (segment.empty()) {
    return false;
  }

  return std::all_of(segment.begin(), segment.end(), [](char character) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    return std::islower(unsigned_character) != 0 || std::isdigit(unsigned_character) != 0 ||
           character == '-' || character == '_';
  });
}

[[nodiscard]] inline bool is_plugin_manifest_dotted_token(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  std::size_t start = 0;
  while (start < value.size()) {
    const auto separator = value.find('.', start);
    const auto segment = value.substr(start, separator == std::string_view::npos
                                                 ? std::string_view::npos
                                                 : separator - start);
    if (!is_plugin_manifest_slug_segment(segment)) {
      return false;
    }

    if (separator == std::string_view::npos) {
      return true;
    }

    start = separator + 1;
  }

  return true;
}

[[nodiscard]] inline bool is_plugin_manifest_platform_tag(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char character) {
    const auto unsigned_character = static_cast<unsigned char>(character);
    return std::islower(unsigned_character) != 0 || std::isdigit(unsigned_character) != 0 ||
           character == '-' || character == '_';
  });
}

[[nodiscard]] inline bool is_plugin_manifest_semver_numeric(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  if (value.size() > 1 && value.front() == '0') {
    return false;
  }

  return std::all_of(value.begin(), value.end(), [](char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
  });
}

[[nodiscard]] inline bool is_plugin_manifest_semver_suffix(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  std::size_t start = 0;
  while (start < value.size()) {
    const auto separator = value.find('.', start);
    const auto segment = value.substr(start, separator == std::string_view::npos
                                                 ? std::string_view::npos
                                                 : separator - start);
    if (segment.empty()) {
      return false;
    }

    const bool valid_segment = std::all_of(segment.begin(), segment.end(), [](char character) {
      const auto unsigned_character = static_cast<unsigned char>(character);
      return std::isalnum(unsigned_character) != 0 || character == '-';
    });
    if (!valid_segment) {
      return false;
    }

    if (separator == std::string_view::npos) {
      return true;
    }

    start = separator + 1;
  }

  return true;
}

[[nodiscard]] inline bool is_plugin_manifest_semver(std::string_view value) {
  if (value.empty()) {
    return false;
  }

  const auto dash = value.find('-');
  const auto plus = value.find('+');
  const auto core_end = std::min(dash == std::string_view::npos ? value.size() : dash,
                                 plus == std::string_view::npos ? value.size() : plus);
  const auto core = value.substr(0, core_end);

  const auto first = core.find('.');
  if (first == std::string_view::npos) {
    return false;
  }

  const auto second = core.find('.', first + 1);
  if (second == std::string_view::npos) {
    return false;
  }

  if (core.find('.', second + 1) != std::string_view::npos) {
    return false;
  }

  if (!is_plugin_manifest_semver_numeric(core.substr(0, first)) ||
      !is_plugin_manifest_semver_numeric(core.substr(first + 1, second - first - 1)) ||
      !is_plugin_manifest_semver_numeric(core.substr(second + 1))) {
    return false;
  }

  if (dash != std::string_view::npos) {
    if (plus != std::string_view::npos && dash > plus) {
      return false;
    }

    const auto prerelease_end = plus == std::string_view::npos ? value.size() : plus;
    if (!is_plugin_manifest_semver_suffix(value.substr(dash + 1, prerelease_end - dash - 1))) {
      return false;
    }
  }

  if (plus != std::string_view::npos &&
      !is_plugin_manifest_semver_suffix(value.substr(plus + 1))) {
    return false;
  }

  return true;
}

[[nodiscard]] inline bool is_plugin_manifest_entry(std::string_view value) {
  return !value.empty() && std::none_of(value.begin(), value.end(), [](char character) {
           return std::isspace(static_cast<unsigned char>(character)) != 0;
         });
}

[[nodiscard]] inline bool is_plugin_manifest_required_abi(std::string_view value) {
  const auto separator = value.find('@');
  if (separator == std::string_view::npos || separator == 0 || separator + 1 >= value.size()) {
    return false;
  }

  return is_plugin_manifest_platform_tag(value.substr(0, separator)) &&
         is_plugin_manifest_semver(value.substr(separator + 1));
}

[[nodiscard]] inline bool uses_allowed_plugin_manifest_extension_namespace(
    std::string_view key) {
  if (!key.starts_with("x.")) {
    return false;
  }

  std::vector<std::string_view> segments;
  std::size_t start = 0;
  while (start < key.size()) {
    const auto separator = key.find('.', start);
    segments.push_back(key.substr(start, separator == std::string_view::npos
                                              ? std::string_view::npos
                                              : separator - start));

    if (separator == std::string_view::npos) {
      break;
    }

    start = separator + 1;
  }

  if (segments.size() < 3 || segments[0] != "x") {
    return false;
  }

  static constexpr std::string_view kReservedOwners[] = {
      "infra", "contracts", "profile", "runtime", "tool", "skill", "plugin"};
  if (std::find(std::begin(kReservedOwners), std::end(kReservedOwners), segments[1]) !=
      std::end(kReservedOwners)) {
    return false;
  }

  return std::all_of(segments.begin() + 1, segments.end(), [](std::string_view segment) {
    return is_plugin_manifest_slug_segment(segment);
  });
}

struct PluginManifestExtension {
  std::string key;
  std::string serialized_value;

  [[nodiscard]] bool is_valid() const {
    return uses_allowed_plugin_manifest_extension_namespace(key) && !serialized_value.empty();
  }
};

struct PluginManifest {
  std::string schema_version = std::string(kPluginUnknownValue);
  std::string plugin_id = std::string(kPluginUnknownValue);
  std::string version = std::string(kPluginUnknownValue);
  std::string entry = std::string(kPluginUnknownValue);
  std::string required_abi = std::string(kPluginUnknownValue);
  std::vector<std::string> capabilities;
  std::string signature_ref = std::string(kPluginUnknownValue);
  std::vector<PluginManifestExtension> extensions;

  [[nodiscard]] bool uses_unknown_defaults() const {
    return schema_version == kPluginUnknownValue && plugin_id == kPluginUnknownValue &&
           version == kPluginUnknownValue && entry == kPluginUnknownValue &&
           required_abi == kPluginUnknownValue && capabilities.empty() &&
           signature_ref == kPluginUnknownValue && extensions.empty();
  }

  [[nodiscard]] bool is_schema_frozen_v1() const {
    return schema_version == "1.0.0";
  }

  [[nodiscard]] bool has_valid_capabilities() const {
    if (capabilities.empty()) {
      return false;
    }

    for (std::size_t index = 0; index < capabilities.size(); ++index) {
      if (!is_plugin_manifest_dotted_token(capabilities[index])) {
        return false;
      }

      for (std::size_t probe = index + 1; probe < capabilities.size(); ++probe) {
        if (capabilities[index] == capabilities[probe]) {
          return false;
        }
      }
    }

    return true;
  }

  [[nodiscard]] bool has_valid_extensions() const {
    for (std::size_t index = 0; index < extensions.size(); ++index) {
      if (!extensions[index].is_valid()) {
        return false;
      }

      for (std::size_t probe = index + 1; probe < extensions.size(); ++probe) {
        if (extensions[index].key == extensions[probe].key) {
          return false;
        }
      }
    }

    return true;
  }

  [[nodiscard]] bool is_valid() const {
    return is_schema_frozen_v1() && plugin_id != kPluginUnknownValue &&
           version != kPluginUnknownValue && entry != kPluginUnknownValue &&
           required_abi != kPluginUnknownValue && signature_ref != kPluginUnknownValue &&
           is_plugin_manifest_dotted_token(plugin_id) && is_plugin_manifest_semver(version) &&
           is_plugin_manifest_entry(entry) && is_plugin_manifest_required_abi(required_abi) &&
           has_valid_capabilities() && has_valid_extensions();
  }

  [[nodiscard]] static PluginManifest normalize(PluginManifest manifest) {
    manifest.schema_version = plugin_value_or_unknown(manifest.schema_version);
    manifest.plugin_id = plugin_value_or_unknown(manifest.plugin_id);
    manifest.version = plugin_value_or_unknown(manifest.version);
    manifest.entry = plugin_value_or_unknown(manifest.entry);
    manifest.required_abi = plugin_value_or_unknown(manifest.required_abi);
    manifest.signature_ref = plugin_value_or_unknown(manifest.signature_ref);
    return manifest;
  }
};

}  // namespace dasall::infra::plugin
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "plugin/PluginManifest.h"

namespace dasall::infra::plugin {

[[nodiscard]] inline bool is_plugin_compatibility_platform_tag_allowed(
    std::string_view platform_tag) {
  return platform_tag == "x86_64-linux-gnu" || platform_tag == "aarch64-linux-gnu" ||
         platform_tag == "armv7-linux-gnueabihf";
}

[[nodiscard]] inline bool has_unique_non_empty_plugin_dependency_refs(
    const std::vector<std::string>& dependency_refs) {
  for (std::size_t index = 0; index < dependency_refs.size(); ++index) {
    if (dependency_refs[index].empty()) {
      return false;
    }

    for (std::size_t probe = index + 1; probe < dependency_refs.size(); ++probe) {
      if (dependency_refs[index] == dependency_refs[probe]) {
        return false;
      }
    }
  }

  return true;
}

[[nodiscard]] inline std::tuple<int, int, int> parse_plugin_semver_core(
    std::string_view value) {
  const auto prerelease = value.find('-');
  const auto build = value.find('+');
  const auto core_end = std::min(prerelease == std::string_view::npos ? value.size() : prerelease,
                                 build == std::string_view::npos ? value.size() : build);
  const auto core = value.substr(0, core_end);
  const auto first = core.find('.');
  const auto second = core.find('.', first + 1);

  return {std::stoi(std::string(core.substr(0, first))),
          std::stoi(std::string(core.substr(first + 1, second - first - 1))),
          std::stoi(std::string(core.substr(second + 1)))};
}

[[nodiscard]] inline bool plugin_abi_version_satisfies_requirement(
    std::string_view host_abi_version,
    std::string_view required_abi_version,
    bool strict_mode) {
  if (!is_plugin_manifest_semver(host_abi_version) || !is_plugin_manifest_semver(required_abi_version)) {
    return false;
  }

  const auto [host_major, host_minor, host_patch] = parse_plugin_semver_core(host_abi_version);
  const auto [required_major, required_minor, required_patch] =
      parse_plugin_semver_core(required_abi_version);

  if (host_major != required_major) {
    return false;
  }

  if (strict_mode) {
    return host_minor == required_minor && host_patch >= required_patch;
  }

  return host_minor > required_minor ||
         (host_minor == required_minor && host_patch >= required_patch);
}

struct PluginHostAbiSnapshot {
  std::string platform_tag = std::string(kPluginUnknownValue);
  std::string abi_version = std::string(kPluginUnknownValue);
  bool strict_mode = true;
  bool api_ready = true;

  [[nodiscard]] bool is_valid() const {
    return platform_tag != kPluginUnknownValue && is_plugin_compatibility_platform_tag_allowed(platform_tag) &&
           abi_version != kPluginUnknownValue && is_plugin_manifest_semver(abi_version);
  }
};

struct PluginDependencyMatrixSnapshot {
  std::vector<std::string> required_dependency_refs;
  std::vector<std::string> available_dependency_refs;

  [[nodiscard]] bool is_valid() const {
    return has_unique_non_empty_plugin_dependency_refs(required_dependency_refs) &&
           has_unique_non_empty_plugin_dependency_refs(available_dependency_refs);
  }

  [[nodiscard]] bool satisfies_required_dependencies() const {
    return std::all_of(required_dependency_refs.begin(), required_dependency_refs.end(),
                       [this](const std::string& dependency_ref) {
                         return std::find(available_dependency_refs.begin(),
                                          available_dependency_refs.end(),
                                          dependency_ref) != available_dependency_refs.end();
                       });
  }
};

struct PluginCompatibilityCheckRequest {
  PluginManifest manifest;
  PluginHostAbiSnapshot host_abi;
  PluginDependencyMatrixSnapshot dependency_matrix;

  [[nodiscard]] bool is_valid() const {
    return manifest.is_valid() && host_abi.is_valid() && dependency_matrix.is_valid();
  }
};

struct CompatibilityReport {
  bool abi_ok = false;
  bool api_ok = false;
  bool dependency_ok = false;
  std::vector<std::string> reason_codes;
  std::string resolved_platform_tag = std::string(kPluginUnknownValue);
  std::string required_abi = std::string(kPluginUnknownValue);
  std::string evidence_ref;

  [[nodiscard]] static CompatibilityReport success(std::string resolved_platform_tag,
                                                   std::string required_abi,
                                                   std::string evidence_ref) {
    return CompatibilityReport{
        .abi_ok = true,
        .api_ok = true,
        .dependency_ok = true,
        .reason_codes = {},
        .resolved_platform_tag = plugin_value_or_unknown(resolved_platform_tag),
        .required_abi = plugin_value_or_unknown(required_abi),
        .evidence_ref = std::move(evidence_ref),
    };
  }

  [[nodiscard]] static CompatibilityReport failure(bool abi_ok,
                                                   bool api_ok,
                                                   bool dependency_ok,
                                                   std::vector<std::string> reason_codes,
                                                   std::string resolved_platform_tag,
                                                   std::string required_abi,
                                                   std::string evidence_ref) {
    return CompatibilityReport{
        .abi_ok = abi_ok,
        .api_ok = api_ok,
        .dependency_ok = dependency_ok,
        .reason_codes = std::move(reason_codes),
        .resolved_platform_tag = plugin_value_or_unknown(resolved_platform_tag),
        .required_abi = plugin_value_or_unknown(required_abi),
        .evidence_ref = std::move(evidence_ref),
    };
  }

  [[nodiscard]] bool is_valid() const {
    if (resolved_platform_tag == kPluginUnknownValue || !is_plugin_manifest_required_abi(required_abi) ||
        evidence_ref.empty() || !has_unique_non_empty_plugin_dependency_refs(reason_codes)) {
      return false;
    }

    if (abi_ok && api_ok && dependency_ok) {
      return reason_codes.empty();
    }

    return !reason_codes.empty();
  }
};

class IPluginCompatibilityEngine {
 public:
  virtual ~IPluginCompatibilityEngine() = default;

  [[nodiscard]] virtual CompatibilityReport check(
      const PluginCompatibilityCheckRequest& request) const = 0;
};

}  // namespace dasall::infra::plugin
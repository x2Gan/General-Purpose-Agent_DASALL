#include "BuildProfileResolver.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "ProfileError.h"
#include "ProfileYamlParser.h"

namespace dasall::profiles {
namespace {

[[nodiscard]] std::optional<bool> parse_bool_value(const std::string& raw_value) {
  if (raw_value == "true") {
    return true;
  }

  if (raw_value == "false") {
    return false;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> infer_toolchain_hint(const std::string& target_platform) {
  if (target_platform.find("arm64") != std::string::npos) {
    return std::string("arm64-linux-gnu");
  }

  if (target_platform.find("x86_64") != std::string::npos) {
    return std::string("x86_64-linux-gnu");
  }

  return std::nullopt;
}

}  // namespace

BuildProfileResolver::BuildProfileResolver(const IProfileCatalog& catalog) : catalog_(catalog) {}

BuildProfileResolveResult BuildProfileResolver::resolve_build_manifest(
    const BuildProfileResolveRequest& request) const {
  if (!request.has_consistent_values()) {
    return BuildProfileResolveResult{
        .manifest = std::nullopt,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

  const ProfileCatalogLookupResult profile_lookup = catalog_.get_profile(request.profile_id);
  if (!profile_lookup.ok()) {
    return BuildProfileResolveResult{
        .manifest = std::nullopt,
        .error_code = profile_lookup.error_code,
    };
  }

  const ProfileDescriptor& descriptor = *profile_lookup.profile;
  if (request.expected_target_platform.has_value() &&
      *request.expected_target_platform != descriptor.target_platform) {
    return BuildProfileResolveResult{
        .manifest = std::nullopt,
        .error_code = ProfileErrorCode::PlatformMismatch,
    };
  }

  const ParsedProfileYaml parsed_yaml = parse_profile_yaml_file(descriptor.asset_paths.runtime_policy_path);
  if (!parsed_yaml.ok) {
    return BuildProfileResolveResult{
        .manifest = std::nullopt,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

  BuildProfileManifest manifest;
  for (const auto& entry : parsed_yaml.scalar_values) {
    const std::string prefix = "enabled_modules.";
    if (!entry.first.starts_with(prefix)) {
      continue;
    }

    const auto enabled = parse_bool_value(entry.second);
    if (!enabled.has_value()) {
      return BuildProfileResolveResult{
          .manifest = std::nullopt,
          .error_code = ProfileErrorCode::SchemaInvalid,
      };
    }

    if (!*enabled) {
      continue;
    }

    const std::string module_name = entry.first.substr(prefix.size());
    manifest.enabled_modules.push_back(module_name);
    if (module_name.ends_with("_adapter")) {
      manifest.enabled_adapters.push_back(module_name);
    }
  }

  std::sort(manifest.enabled_modules.begin(), manifest.enabled_modules.end());
  std::sort(manifest.enabled_adapters.begin(), manifest.enabled_adapters.end());

  const bool observability_enabled =
      std::find(manifest.enabled_modules.begin(), manifest.enabled_modules.end(),
                "infra_observability") != manifest.enabled_modules.end();

  manifest.observability_level = observability_enabled ? "full" : "minimal";
  manifest.build_tags = {
      "profile:" + descriptor.profile_id,
      "platform:" + descriptor.target_platform,
      "support:" + descriptor.support_level,
  };
  manifest.toolchain_hint = infer_toolchain_hint(descriptor.target_platform);

  if (!manifest.has_consistent_values()) {
    return BuildProfileResolveResult{
        .manifest = std::nullopt,
        .error_code = ProfileErrorCode::ModuleIncompatible,
    };
  }

  return BuildProfileResolveResult{
      .manifest = manifest,
      .error_code = std::nullopt,
  };
}

}  // namespace dasall::profiles

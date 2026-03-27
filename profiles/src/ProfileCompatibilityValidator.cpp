#include "ProfileCompatibilityValidator.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace dasall::profiles {
namespace {

[[nodiscard]] bool has_name(const std::vector<std::string>& names, std::string_view expected) {
  return std::find(names.begin(), names.end(), expected) != names.end();
}

[[nodiscard]] std::unordered_set<std::string> frozen_module_name_set() {
  return {
      "runtime",
      "cognition",
      "llm_cloud_adapter",
      "llm_lan_adapter",
      "llm_local_adapter",
      "tools_builtin",
      "tools_mcp",
      "memory_vector",
      "memory_experience",
      "knowledge",
      "multi_agent",
      "platform_hal",
      "infra_observability",
  };
}

[[nodiscard]] std::string infer_target_platform_from_manifest(const BuildProfileManifest& manifest) {
  constexpr std::string_view kPrefix = "platform:";
  for (const std::string& tag : manifest.build_tags) {
    if (tag.starts_with(kPrefix) && tag.size() > kPrefix.size()) {
      return tag.substr(kPrefix.size());
    }
  }

  return "";
}

}  // namespace

ValidationReport ProfileCompatibilityValidator::validate(
    const RuntimePolicySnapshot& candidate,
    const BuildProfileManifest& build_manifest,
    const ProfileRuntimeEnvironment& environment) const {
  ValidationReport report;

  if (!candidate.has_consistent_values() || !build_manifest.has_consistent_values() ||
      !environment.has_consistent_values()) {
    report.blocking_errors.push_back(ProfileErrorCode::SchemaInvalid);
    report.compatibility_state = ProfileCompatibilityState::Blocked;
    report.dependency_gaps.push_back("inconsistent-validator-input");
    return report;
  }

  const std::unordered_set<std::string> frozen_module_names = frozen_module_name_set();

  const std::string expected_platform = infer_target_platform_from_manifest(build_manifest);
  if (!expected_platform.empty() && expected_platform != environment.target_platform) {
    report.blocking_errors.push_back(ProfileErrorCode::PlatformMismatch);
    report.dependency_gaps.push_back("target-platform-mismatch");
  }

  for (const std::string& module_name : build_manifest.enabled_modules) {
    if (!frozen_module_names.contains(module_name)) {
      report.blocking_errors.push_back(ProfileErrorCode::ModuleIncompatible);
      report.dependency_gaps.push_back("unknown-module:" + module_name);
      continue;
    }

    if (has_name(environment.available_modules, module_name)) {
      continue;
    }

    if (module_name == "infra_observability") {
      report.warnings.push_back(ProfileErrorCode::OverrideInvalid);
      report.dependency_gaps.push_back("optional-module-missing:" + module_name);
      continue;
    }

    report.blocking_errors.push_back(ProfileErrorCode::ModuleIncompatible);
    report.dependency_gaps.push_back("required-module-missing:" + module_name);
  }

  for (const std::string& adapter_name : build_manifest.enabled_adapters) {
    if (!has_name(environment.available_adapters, adapter_name)) {
      report.blocking_errors.push_back(ProfileErrorCode::RequiredAdapterMissing);
      report.dependency_gaps.push_back("required-adapter-missing:" + adapter_name);
    }
  }

  if (!report.blocking_errors.empty()) {
    report.compatibility_state = ProfileCompatibilityState::Blocked;
  } else if (!report.warnings.empty()) {
    report.compatibility_state = ProfileCompatibilityState::Warning;
  } else {
    report.compatibility_state = ProfileCompatibilityState::Compatible;
  }

  return report;
}

}  // namespace dasall::profiles

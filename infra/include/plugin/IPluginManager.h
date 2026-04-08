#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "plugin/PluginCatalog.h"
#include "plugin/PluginReports.h"
#include "policy/PolicyDecisionRef.h"

namespace dasall::infra::plugin {

enum class PluginOperationPhase {
  Unknown = 0,
  Validate = 1,
  Load = 2,
  Activate = 3,
  Unload = 4,
};

inline constexpr std::string_view plugin_operation_phase_name(PluginOperationPhase phase) {
  switch (phase) {
    case PluginOperationPhase::Unknown:
      return "unknown";
    case PluginOperationPhase::Validate:
      return "validate";
    case PluginOperationPhase::Load:
      return "load";
    case PluginOperationPhase::Activate:
      return "activate";
    case PluginOperationPhase::Unload:
      return "unload";
  }

  return "unknown";
}

struct PluginValidationRequest {
  std::string plugin_id = std::string(kPluginUnknownValue);
  std::string manifest_ref = std::string(kPluginUnknownValue);
  std::string package_ref = std::string(kPluginUnknownValue);
  std::string profile_id = std::string(kPluginUnknownValue);

  [[nodiscard]] bool is_valid() const {
    return plugin_id != kPluginUnknownValue && manifest_ref != kPluginUnknownValue &&
           package_ref != kPluginUnknownValue && profile_id != kPluginUnknownValue;
  }
};

struct PluginLoadOptions {
  std::string profile_id = std::string(kPluginUnknownValue);
  std::string actor_ref = std::string(kPluginUnknownValue);
  std::uint32_t timeout_ms = 3000;
  bool audit_required = true;
  bool dry_run = false;

  [[nodiscard]] bool is_valid() const {
    return profile_id != kPluginUnknownValue && actor_ref != kPluginUnknownValue &&
           timeout_ms > 0;
  }
};

struct PluginValidationResult {
  bool accepted = false;
  std::string plugin_id = std::string(kPluginUnknownValue);
  policy::PolicyDecisionRef policy_decision;
  std::string signature_report_ref;
  std::string compatibility_report_ref;
  std::optional<SignatureReport> signature_report;
  std::optional<CompatibilityReport> compatibility_report;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static PluginValidationResult success(
      std::string plugin_id,
      policy::PolicyDecisionRef policy_decision,
      std::string signature_report_ref,
      std::string compatibility_report_ref,
      std::string evidence_ref,
      std::optional<SignatureReport> signature_report = std::nullopt,
      std::optional<CompatibilityReport> compatibility_report = std::nullopt) {
    return PluginValidationResult{
        .accepted = true,
        .plugin_id = plugin_value_or_unknown(plugin_id),
        .policy_decision = std::move(policy_decision),
        .signature_report_ref = std::move(signature_report_ref),
        .compatibility_report_ref = std::move(compatibility_report_ref),
        .signature_report = std::move(signature_report),
        .compatibility_report = std::move(compatibility_report),
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static PluginValidationResult failure(contracts::ResultCode result_code,
                                                      std::string plugin_id,
                                                      std::string message,
                                                      std::string stage,
                                                      std::string source_ref,
                              std::string evidence_ref,
                              std::string signature_report_ref = {},
                              std::string compatibility_report_ref = {},
                              std::optional<SignatureReport> signature_report = std::nullopt,
                              std::optional<CompatibilityReport> compatibility_report = std::nullopt) {
    return PluginValidationResult{
        .accepted = false,
        .plugin_id = plugin_value_or_unknown(plugin_id),
        .policy_decision = {},
      .signature_report_ref = std::move(signature_report_ref),
      .compatibility_report_ref = std::move(compatibility_report_ref),
      .signature_report = std::move(signature_report),
      .compatibility_report = std::move(compatibility_report),
        .evidence_ref = std::move(evidence_ref),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.plugin",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool has_traceable_refs() const {
    if (plugin_id == kPluginUnknownValue || evidence_ref.empty()) {
      return false;
    }

    if (signature_report.has_value() &&
        (signature_report_ref.empty() || !signature_report->is_valid())) {
      return false;
    }

    if (compatibility_report.has_value() &&
        (compatibility_report_ref.empty() || !compatibility_report->is_valid())) {
      return false;
    }

    return accepted ? policy_decision.is_valid() && !signature_report_ref.empty() &&
                          !compatibility_report_ref.empty()
                    : true;
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return accepted;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

struct PluginLoadResult {
  bool loaded = false;
  std::string plugin_id = std::string(kPluginUnknownValue);
  PluginOperationPhase phase = PluginOperationPhase::Unknown;
  std::string handle_ref;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static PluginLoadResult success(std::string plugin_id,
                                                PluginOperationPhase phase,
                                                std::string handle_ref,
                                                std::string evidence_ref) {
    return PluginLoadResult{
        .loaded = true,
        .plugin_id = plugin_value_or_unknown(plugin_id),
        .phase = phase,
        .handle_ref = std::move(handle_ref),
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static PluginLoadResult failure(contracts::ResultCode result_code,
                                                std::string plugin_id,
                                                PluginOperationPhase phase,
                                                std::string message,
                                                std::string stage,
                                                std::string source_ref,
                                                std::string evidence_ref) {
    return PluginLoadResult{
        .loaded = false,
        .plugin_id = plugin_value_or_unknown(plugin_id),
        .phase = phase,
        .handle_ref = {},
        .evidence_ref = std::move(evidence_ref),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.plugin",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return loaded;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

struct PluginUnloadResult {
  bool unloaded = false;
  std::string plugin_id = std::string(kPluginUnknownValue);
  PluginOperationPhase phase = PluginOperationPhase::Unload;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static PluginUnloadResult success(std::string plugin_id,
                                                  std::string evidence_ref) {
    return PluginUnloadResult{
        .unloaded = true,
        .plugin_id = plugin_value_or_unknown(plugin_id),
        .phase = PluginOperationPhase::Unload,
        .evidence_ref = std::move(evidence_ref),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static PluginUnloadResult failure(contracts::ResultCode result_code,
                                                  std::string plugin_id,
                                                  std::string message,
                                                  std::string stage,
                                                  std::string source_ref,
                                                  std::string evidence_ref) {
    return PluginUnloadResult{
        .unloaded = false,
        .plugin_id = plugin_value_or_unknown(plugin_id),
        .phase = PluginOperationPhase::Unload,
        .evidence_ref = std::move(evidence_ref),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.plugin",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return unloaded;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

struct ActivePluginSet {
  std::vector<PluginDescriptor> active_plugins;
  bool safe_mode_active = false;
  std::size_t max_active = 0;

  [[nodiscard]] bool has_consistent_entries() const {
    if (!has_unique_plugin_ids(active_plugins)) {
      return false;
    }

    if (max_active != 0 && active_plugins.size() > max_active) {
      return false;
    }

    return std::all_of(active_plugins.begin(), active_plugins.end(), [](const PluginDescriptor& descriptor) {
      return descriptor.status == PluginStatus::Loaded || descriptor.status == PluginStatus::Active ||
             descriptor.status == PluginStatus::Disabled;
    });
  }
};

class IPluginManager {
 public:
  virtual ~IPluginManager() = default;

  [[nodiscard]] virtual PluginCatalog discover(std::string_view profile_id) const = 0;
  [[nodiscard]] virtual PluginValidationResult validate(
      const PluginValidationRequest& request) const = 0;
  virtual PluginLoadResult load(std::string_view plugin_id,
                                const PluginLoadOptions& load_options) = 0;
  virtual PluginUnloadResult unload(std::string_view plugin_id) = 0;
  [[nodiscard]] virtual ActivePluginSet list_active() const = 0;
};

}  // namespace dasall::infra::plugin
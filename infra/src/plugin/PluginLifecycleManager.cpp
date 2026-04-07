#include "plugin/PluginLifecycleManager.h"

#include <algorithm>
#include <utility>

#include "plugin/PluginAuditAdapter.h"
#include "plugin/PluginErrorCode.h"

namespace dasall::infra::plugin {
namespace {

[[nodiscard]] std::string make_evidence_ref(std::string_view action,
                                            std::string_view plugin_id,
                                            std::string_view suffix) {
  return std::string(action) + "." + plugin_value_or_unknown(plugin_id) + "." +
         std::string(suffix);
}

[[nodiscard]] std::string normalized_reason_code(std::string reason_code,
                                                 std::string_view fallback) {
  if (reason_code.empty()) {
    return std::string(fallback);
  }

  return reason_code;
}

[[nodiscard]] PluginDescriptor make_managed_descriptor(std::string_view plugin_id,
                                                       PluginStatus status) {
  return PluginDescriptor::normalize(PluginDescriptor{
      .plugin_id = plugin_value_or_unknown(plugin_id),
      .version = std::string("skeleton"),
      .abi = std::string("skeleton"),
      .trust_level = PluginTrustLevel::Internal,
      .status = status,
      .source = std::string("runtime://plugin/") + plugin_value_or_unknown(plugin_id),
  });
}

[[nodiscard]] contracts::ResultCode lifecycle_result_code(PluginErrorCode code) {
  return map_plugin_error_code(code).result_code;
}

}  // namespace

PluginLifecycleTransitionResult PluginLifecycleTransitionResult::success(
    std::string plugin_id,
    PluginStatus from_status,
    PluginStatus to_status,
    std::string evidence_ref) {
  return PluginLifecycleTransitionResult{
      .transitioned = true,
      .plugin_id = plugin_value_or_unknown(plugin_id),
      .from_status = from_status,
      .to_status = to_status,
      .evidence_ref = std::move(evidence_ref),
      .result_code = std::nullopt,
      .error_info = std::nullopt,
  };
}

PluginLifecycleTransitionResult PluginLifecycleTransitionResult::failure(
    contracts::ResultCode result_code,
    std::string plugin_id,
    PluginStatus from_status,
    PluginStatus to_status,
    std::string message,
    std::string stage,
    std::string source_ref,
    std::string evidence_ref) {
  return PluginLifecycleTransitionResult{
      .transitioned = false,
      .plugin_id = plugin_value_or_unknown(plugin_id),
      .from_status = from_status,
      .to_status = to_status,
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

bool PluginLifecycleTransitionResult::references_only_contract_error_types() const {
  if (!error_info.has_value()) {
    return transitioned && !result_code.has_value();
  }

  return result_code.has_value() && error_info->failure_type.has_value() &&
         *error_info->failure_type == contracts::classify_result_code(*result_code);
}

PluginRuntimeLoadResult PluginRuntimeLoadResult::success(std::string handle_ref,
                                                         std::string evidence_ref,
                                                         std::string reason_code) {
  return PluginRuntimeLoadResult{
      .loaded = true,
      .handle_ref = std::move(handle_ref),
      .evidence_ref = std::move(evidence_ref),
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason_code = std::move(reason_code),
      .message = {},
  };
}

PluginRuntimeLoadResult PluginRuntimeLoadResult::failure(
    contracts::ResultCode result_code,
    std::string reason_code,
    std::string evidence_ref,
    std::string message) {
  return PluginRuntimeLoadResult{
      .loaded = false,
      .handle_ref = {},
      .evidence_ref = std::move(evidence_ref),
      .result_code = result_code,
      .reason_code = std::move(reason_code),
      .message = std::move(message),
  };
}

bool PluginRuntimeLoadResult::is_valid() const {
  if (loaded) {
    return !handle_ref.empty() && !evidence_ref.empty();
  }

  return !evidence_ref.empty() && !reason_code.empty() && !message.empty() &&
         contracts::classify_result_code(result_code) !=
             contracts::ResultCodeCategory::Unknown;
}

PluginRuntimeUnloadResult PluginRuntimeUnloadResult::success(std::string evidence_ref,
                                                             std::string reason_code) {
  return PluginRuntimeUnloadResult{
      .unloaded = true,
      .evidence_ref = std::move(evidence_ref),
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .reason_code = std::move(reason_code),
      .message = {},
  };
}

PluginRuntimeUnloadResult PluginRuntimeUnloadResult::failure(
    contracts::ResultCode result_code,
    std::string reason_code,
    std::string evidence_ref,
    std::string message) {
  return PluginRuntimeUnloadResult{
      .unloaded = false,
      .evidence_ref = std::move(evidence_ref),
      .result_code = result_code,
      .reason_code = std::move(reason_code),
      .message = std::move(message),
  };
}

bool PluginRuntimeUnloadResult::is_valid() const {
  if (unloaded) {
    return !evidence_ref.empty();
  }

  return !evidence_ref.empty() && !reason_code.empty() && !message.empty() &&
         contracts::classify_result_code(result_code) !=
             contracts::ResultCodeCategory::Unknown;
}

PluginLifecycleManager::PluginLifecycleManager(
    PluginRuntimeLoadCallback runtime_load,
    PluginRuntimeUnloadCallback runtime_unload,
    PluginAuditAdapter* audit_adapter,
    std::size_t max_active,
    std::size_t safe_mode_fail_threshold)
    : runtime_load_(runtime_load ? std::move(runtime_load) : default_runtime_load),
      runtime_unload_(runtime_unload ? std::move(runtime_unload)
                                     : default_runtime_unload),
      audit_adapter_(audit_adapter),
      max_active_(max_active),
      safe_mode_fail_threshold_(safe_mode_fail_threshold == 0 ? 1
                                                              : safe_mode_fail_threshold) {}

PluginLoadResult PluginLifecycleManager::load(std::string_view plugin_id,
                                              const PluginLoadOptions& load_options) {
  if (plugin_id.empty() || !load_options.is_valid()) {
    return make_load_failure(plugin_id,
                             load_options.actor_ref,
                             contracts::ResultCode::ValidationFieldMissing,
                             std::string("plugin_load_invalid_request"),
                             std::string(
                                 "plugin lifecycle load requires a plugin_id and valid load options"),
                             make_evidence_ref("plugin.load", plugin_id,
                                               "invalid-request"));
  }

  if (safe_mode_active_) {
    return make_load_failure(plugin_id,
                             load_options.actor_ref,
                             lifecycle_result_code(PluginErrorCode::LoadFail),
                             std::string("plugin_load_safe_mode"),
                             std::string(
                                 "plugin lifecycle load is blocked while plugin_safe_mode is active"),
                             make_evidence_ref("plugin.load", plugin_id, "safe-mode"));
  }

  if (find_plugin(plugin_id) != nullptr) {
    return make_load_failure(plugin_id,
                             load_options.actor_ref,
                             lifecycle_result_code(PluginErrorCode::LoadFail),
                             std::string("plugin_load_already_managed"),
                             std::string(
                                 "plugin lifecycle load rejected an already managed plugin"),
                             make_evidence_ref("plugin.load", plugin_id,
                                               "already-managed"));
  }

  if (max_active_ != 0 && managed_plugins_.size() >= max_active_) {
    return make_load_failure(plugin_id,
                             load_options.actor_ref,
                             lifecycle_result_code(PluginErrorCode::LoadFail),
                             std::string("plugin_load_capacity_exhausted"),
                             std::string(
                                 "plugin lifecycle load exceeded the configured max_active capacity"),
                             make_evidence_ref("plugin.load", plugin_id,
                                               "max-active"));
  }

  if (load_options.dry_run) {
    const auto evidence_ref = make_evidence_ref("plugin.load", plugin_id, "dry-run");
    record_success();
    emit_load_audit(load_options.actor_ref,
                    plugin_id,
                    true,
                    evidence_ref,
                    std::string("plugin_load_dry_run"),
                    std::nullopt);
    return PluginLoadResult::success(std::string(plugin_id),
                                     PluginOperationPhase::Load,
                                     std::string("dry-run:") +
                                         plugin_value_or_unknown(plugin_id),
                                     evidence_ref);
  }

  const auto runtime_result = runtime_load_(plugin_id, load_options);
  if (!runtime_result.is_valid()) {
    return make_load_failure(plugin_id,
                             load_options.actor_ref,
                             lifecycle_result_code(PluginErrorCode::LoadFail),
                             std::string("plugin_load_invalid_runtime_result"),
                             std::string(
                                 "plugin lifecycle runtime bridge returned an invalid load outcome"),
                             make_evidence_ref("plugin.load", plugin_id,
                                               "invalid-runtime-result"));
  }

  if (!runtime_result.loaded) {
    return make_load_failure(
        plugin_id,
        load_options.actor_ref,
        runtime_result.result_code,
        normalized_reason_code(runtime_result.reason_code, "plugin_load_failed"),
        runtime_result.message,
        runtime_result.evidence_ref);
  }

  managed_plugins_.push_back(ManagedPlugin{
      .descriptor = make_managed_descriptor(plugin_id, PluginStatus::Loaded),
      .handle_ref = runtime_result.handle_ref,
      .actor_ref = plugin_value_or_unknown(load_options.actor_ref),
  });

  record_success();
  emit_load_audit(load_options.actor_ref,
                  plugin_id,
                  true,
                  runtime_result.evidence_ref,
                  normalized_reason_code(runtime_result.reason_code,
                                         "plugin_load_succeeded"),
                  std::nullopt);
  return PluginLoadResult::success(std::string(plugin_id),
                                   PluginOperationPhase::Load,
                                   runtime_result.handle_ref,
                                   runtime_result.evidence_ref);
}

PluginUnloadResult PluginLifecycleManager::unload(std::string_view plugin_id) {
  if (plugin_id.empty()) {
    return make_unload_failure(plugin_id,
                               std::string(kPluginUnknownValue),
                               contracts::ResultCode::ValidationFieldMissing,
                               std::string("plugin_unload_invalid_request"),
                               std::string("plugin lifecycle unload requires a plugin_id"),
                               make_evidence_ref("plugin.unload", plugin_id,
                                                 "invalid-request"));
  }

  auto* managed_plugin = find_plugin(plugin_id);
  if (managed_plugin == nullptr) {
    return make_unload_failure(plugin_id,
                               std::string(kPluginUnknownValue),
                               lifecycle_result_code(PluginErrorCode::UnloadFail),
                               std::string("plugin_unload_not_loaded"),
                               std::string(
                                   "plugin lifecycle unload requires an already managed plugin"),
                               make_evidence_ref("plugin.unload", plugin_id,
                                                 "not-loaded"));
  }

  const auto runtime_result =
      runtime_unload_(plugin_id, managed_plugin->handle_ref);
  if (!runtime_result.is_valid()) {
    return make_unload_failure(plugin_id,
                               managed_plugin->actor_ref,
                               lifecycle_result_code(PluginErrorCode::UnloadFail),
                               std::string("plugin_unload_invalid_runtime_result"),
                               std::string(
                                   "plugin lifecycle runtime bridge returned an invalid unload outcome"),
                               make_evidence_ref("plugin.unload", plugin_id,
                                                 "invalid-runtime-result"));
  }

  if (!runtime_result.unloaded) {
    return make_unload_failure(
        plugin_id,
        managed_plugin->actor_ref,
        runtime_result.result_code,
        normalized_reason_code(runtime_result.reason_code, "plugin_unload_failed"),
        runtime_result.message,
        runtime_result.evidence_ref);
  }

  emit_unload_audit(managed_plugin->actor_ref,
                    plugin_id,
                    true,
                    runtime_result.evidence_ref,
                    normalized_reason_code(runtime_result.reason_code,
                                           "plugin_unload_succeeded"),
                    std::nullopt);
  managed_plugins_.erase(
      std::remove_if(managed_plugins_.begin(),
                     managed_plugins_.end(),
                     [plugin_id](const ManagedPlugin& plugin) {
                       return plugin.descriptor.plugin_id == plugin_id;
                     }),
      managed_plugins_.end());
  record_success();
  return PluginUnloadResult::success(std::string(plugin_id), runtime_result.evidence_ref);
}

PluginLifecycleTransitionResult PluginLifecycleManager::enable(
    std::string_view plugin_id) {
  auto* managed_plugin = find_plugin(plugin_id);
  if (managed_plugin == nullptr) {
    return make_transition_failure(plugin_id,
                                   PluginStatus::Unknown,
                                   PluginStatus::Active,
                                   std::string("plugin.enable"),
                                   std::string("plugin_enable_not_loaded"),
                                   std::string(
                                       "plugin lifecycle enable requires a loaded or disabled plugin"),
                                   make_evidence_ref("plugin.enable", plugin_id,
                                                     "not-loaded"));
  }

  if (managed_plugin->descriptor.status != PluginStatus::Loaded &&
      managed_plugin->descriptor.status != PluginStatus::Disabled) {
    return make_transition_failure(
        plugin_id,
        managed_plugin->descriptor.status,
        PluginStatus::Active,
        std::string("plugin.enable"),
        std::string("plugin_enable_invalid_state"),
        std::string(
            "plugin lifecycle enable only accepts Loaded or Disabled states"),
        make_evidence_ref("plugin.enable", plugin_id, "invalid-state"));
  }

  const auto from_status = managed_plugin->descriptor.status;
  managed_plugin->descriptor.status = PluginStatus::Active;
  return PluginLifecycleTransitionResult::success(std::string(plugin_id),
                                                  from_status,
                                                  PluginStatus::Active,
                                                  make_evidence_ref("plugin.enable",
                                                                    plugin_id,
                                                                    "active"));
}

PluginLifecycleTransitionResult PluginLifecycleManager::disable(
    std::string_view plugin_id) {
  auto* managed_plugin = find_plugin(plugin_id);
  if (managed_plugin == nullptr) {
    return make_transition_failure(plugin_id,
                                   PluginStatus::Unknown,
                                   PluginStatus::Disabled,
                                   std::string("plugin.disable"),
                                   std::string("plugin_disable_not_loaded"),
                                   std::string(
                                       "plugin lifecycle disable requires a loaded or active plugin"),
                                   make_evidence_ref("plugin.disable", plugin_id,
                                                     "not-loaded"));
  }

  if (managed_plugin->descriptor.status != PluginStatus::Loaded &&
      managed_plugin->descriptor.status != PluginStatus::Active) {
    return make_transition_failure(
        plugin_id,
        managed_plugin->descriptor.status,
        PluginStatus::Disabled,
        std::string("plugin.disable"),
        std::string("plugin_disable_invalid_state"),
        std::string(
            "plugin lifecycle disable only accepts Loaded or Active states"),
        make_evidence_ref("plugin.disable", plugin_id, "invalid-state"));
  }

  const auto from_status = managed_plugin->descriptor.status;
  managed_plugin->descriptor.status = PluginStatus::Disabled;
  return PluginLifecycleTransitionResult::success(std::string(plugin_id),
                                                  from_status,
                                                  PluginStatus::Disabled,
                                                  make_evidence_ref("plugin.disable",
                                                                    plugin_id,
                                                                    "disabled"));
}

ActivePluginSet PluginLifecycleManager::list_active() const {
  ActivePluginSet active_set{
      .active_plugins = {},
      .safe_mode_active = safe_mode_active_,
      .max_active = max_active_,
  };
  active_set.active_plugins.reserve(managed_plugins_.size());
  for (const auto& managed_plugin : managed_plugins_) {
    active_set.active_plugins.push_back(managed_plugin.descriptor);
  }
  return active_set;
}

std::optional<PluginStatus> PluginLifecycleManager::current_status(
    std::string_view plugin_id) const {
  const auto* managed_plugin = find_plugin(plugin_id);
  if (managed_plugin == nullptr) {
    return std::nullopt;
  }

  return managed_plugin->descriptor.status;
}

PluginRuntimeLoadResult PluginLifecycleManager::default_runtime_load(
    std::string_view plugin_id,
    const PluginLoadOptions& load_options) {
  static_cast<void>(load_options);
  return PluginRuntimeLoadResult::success(
      std::string("handle:") + plugin_value_or_unknown(plugin_id),
      make_evidence_ref("plugin.load", plugin_id, "loaded"),
      std::string("plugin_load_succeeded"));
}

PluginRuntimeUnloadResult PluginLifecycleManager::default_runtime_unload(
    std::string_view plugin_id,
    std::string_view handle_ref) {
  static_cast<void>(handle_ref);
  return PluginRuntimeUnloadResult::success(
      make_evidence_ref("plugin.unload", plugin_id, "unloaded"),
      std::string("plugin_unload_succeeded"));
}

PluginLifecycleManager::ManagedPlugin* PluginLifecycleManager::find_plugin(
    std::string_view plugin_id) {
  const auto it = std::find_if(
      managed_plugins_.begin(),
      managed_plugins_.end(),
      [plugin_id](const ManagedPlugin& plugin) {
        return plugin.descriptor.plugin_id == plugin_id;
      });
  if (it == managed_plugins_.end()) {
    return nullptr;
  }

  return &(*it);
}

const PluginLifecycleManager::ManagedPlugin* PluginLifecycleManager::find_plugin(
    std::string_view plugin_id) const {
  const auto it = std::find_if(
      managed_plugins_.begin(),
      managed_plugins_.end(),
      [plugin_id](const ManagedPlugin& plugin) {
        return plugin.descriptor.plugin_id == plugin_id;
      });
  if (it == managed_plugins_.end()) {
    return nullptr;
  }

  return &(*it);
}

PluginLoadResult PluginLifecycleManager::make_load_failure(
    std::string_view plugin_id,
    std::string_view actor_ref,
    contracts::ResultCode result_code,
    std::string reason_code,
    std::string message,
    std::string evidence_ref) {
  record_failure();
  emit_load_audit(actor_ref,
                  plugin_id,
                  false,
                  evidence_ref,
                  reason_code,
                  result_code);
  return PluginLoadResult::failure(result_code,
                                   std::string(plugin_id),
                                   PluginOperationPhase::Load,
                                   std::move(message),
                                   std::string("plugin.load"),
                                   std::string("PluginLifecycleManager"),
                                   std::move(evidence_ref));
}

PluginUnloadResult PluginLifecycleManager::make_unload_failure(
    std::string_view plugin_id,
    std::string_view actor_ref,
    contracts::ResultCode result_code,
    std::string reason_code,
    std::string message,
    std::string evidence_ref) {
  record_failure();
  emit_unload_audit(actor_ref,
                    plugin_id,
                    false,
                    evidence_ref,
                    reason_code,
                    result_code);
  return PluginUnloadResult::failure(result_code,
                                     std::string(plugin_id),
                                     std::move(message),
                                     std::string("plugin.unload"),
                                     std::string("PluginLifecycleManager"),
                                     std::move(evidence_ref));
}

PluginLifecycleTransitionResult PluginLifecycleManager::make_transition_failure(
    std::string_view plugin_id,
    PluginStatus from_status,
    PluginStatus to_status,
    std::string stage,
    std::string reason_code,
    std::string message,
    std::string evidence_ref) const {
  static_cast<void>(reason_code);
  return PluginLifecycleTransitionResult::failure(
      lifecycle_result_code(PluginErrorCode::LoadFail),
      std::string(plugin_id),
      from_status,
      to_status,
      std::move(message),
      std::move(stage),
      std::string("PluginLifecycleManager"),
      std::move(evidence_ref));
}

void PluginLifecycleManager::record_success() {
  consecutive_failures_ = 0;
}

void PluginLifecycleManager::record_failure() {
  ++consecutive_failures_;
  if (consecutive_failures_ >= safe_mode_fail_threshold_) {
    safe_mode_active_ = true;
  }
}

void PluginLifecycleManager::emit_load_audit(
    std::string_view actor_ref,
    std::string_view plugin_id,
    bool succeeded,
    std::string_view evidence_ref,
    std::string_view reason_code,
    std::optional<contracts::ResultCode> result_code) {
  if (audit_adapter_ == nullptr) {
    return;
  }

  PluginAuditRecord record{
      .actor_ref = plugin_value_or_unknown(actor_ref),
      .plugin_id = plugin_value_or_unknown(plugin_id),
      .succeeded = succeeded,
      .evidence_ref = std::string(evidence_ref),
      .reason_code = normalized_reason_code(std::string(reason_code),
                                            succeeded ? "plugin_load_succeeded"
                                                      : "plugin_load_failed"),
      .result_code = result_code,
      .request_id = std::nullopt,
      .trace_id = std::nullopt,
      .task_id = std::nullopt,
  };
  static_cast<void>(audit_adapter_->write_load_audit(std::move(record)));
}

void PluginLifecycleManager::emit_unload_audit(
    std::string_view actor_ref,
    std::string_view plugin_id,
    bool succeeded,
    std::string_view evidence_ref,
    std::string_view reason_code,
    std::optional<contracts::ResultCode> result_code) {
  if (audit_adapter_ == nullptr) {
    return;
  }

  PluginAuditRecord record{
      .actor_ref = plugin_value_or_unknown(actor_ref),
      .plugin_id = plugin_value_or_unknown(plugin_id),
      .succeeded = succeeded,
      .evidence_ref = std::string(evidence_ref),
      .reason_code = normalized_reason_code(std::string(reason_code),
                                            succeeded ? "plugin_unload_succeeded"
                                                      : "plugin_unload_failed"),
      .result_code = result_code,
      .request_id = std::nullopt,
      .trace_id = std::nullopt,
      .task_id = std::nullopt,
  };
  static_cast<void>(audit_adapter_->write_unload_audit(std::move(record)));
}

}  // namespace dasall::infra::plugin
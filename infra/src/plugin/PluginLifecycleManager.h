#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugin/IPluginManager.h"

namespace dasall::infra::plugin {

class PluginAuditAdapter;

struct PluginLifecycleTransitionResult {
  bool transitioned = false;
  std::string plugin_id = std::string(kPluginUnknownValue);
  PluginStatus from_status = PluginStatus::Unknown;
  PluginStatus to_status = PluginStatus::Unknown;
  std::string evidence_ref;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static PluginLifecycleTransitionResult success(
      std::string plugin_id,
      PluginStatus from_status,
      PluginStatus to_status,
      std::string evidence_ref);

  [[nodiscard]] static PluginLifecycleTransitionResult failure(
      contracts::ResultCode result_code,
      std::string plugin_id,
      PluginStatus from_status,
      PluginStatus to_status,
      std::string message,
      std::string stage,
      std::string source_ref,
      std::string evidence_ref);

  [[nodiscard]] bool references_only_contract_error_types() const;
};

struct PluginRuntimeLoadResult {
  bool loaded = false;
  std::string handle_ref;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::string reason_code;
  std::string message;

  [[nodiscard]] static PluginRuntimeLoadResult success(
      std::string handle_ref,
      std::string evidence_ref,
      std::string reason_code = {});

  [[nodiscard]] static PluginRuntimeLoadResult failure(
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string evidence_ref,
      std::string message);

  [[nodiscard]] bool is_valid() const;
};

struct PluginRuntimeUnloadResult {
  bool unloaded = false;
  std::string evidence_ref;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::string reason_code;
  std::string message;

  [[nodiscard]] static PluginRuntimeUnloadResult success(
      std::string evidence_ref,
      std::string reason_code = {});

  [[nodiscard]] static PluginRuntimeUnloadResult failure(
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string evidence_ref,
      std::string message);

  [[nodiscard]] bool is_valid() const;
};

using PluginRuntimeLoadCallback =
    std::function<PluginRuntimeLoadResult(std::string_view,
                                          const PluginLoadOptions&)>;
using PluginRuntimeUnloadCallback =
    std::function<PluginRuntimeUnloadResult(std::string_view, std::string_view)>;

class PluginLifecycleManager {
 public:
  PluginLifecycleManager(PluginRuntimeLoadCallback runtime_load = {},
                         PluginRuntimeUnloadCallback runtime_unload = {},
                         PluginAuditAdapter* audit_adapter = nullptr,
                         std::size_t max_active = 16,
                         std::size_t safe_mode_fail_threshold = 3);

  [[nodiscard]] PluginLoadResult load(std::string_view plugin_id,
                                      const PluginLoadOptions& load_options);
  [[nodiscard]] PluginUnloadResult unload(std::string_view plugin_id);
  [[nodiscard]] PluginLifecycleTransitionResult enable(std::string_view plugin_id);
  [[nodiscard]] PluginLifecycleTransitionResult disable(std::string_view plugin_id);
  [[nodiscard]] ActivePluginSet list_active() const;

  [[nodiscard]] bool safe_mode_active() const {
    return safe_mode_active_;
  }

  [[nodiscard]] std::size_t consecutive_failures() const {
    return consecutive_failures_;
  }

  [[nodiscard]] std::optional<PluginStatus> current_status(
      std::string_view plugin_id) const;

 private:
  struct ManagedPlugin {
    PluginDescriptor descriptor;
    std::string handle_ref;
    std::string actor_ref;
  };

  [[nodiscard]] static PluginRuntimeLoadResult default_runtime_load(
      std::string_view plugin_id,
      const PluginLoadOptions& load_options);
  [[nodiscard]] static PluginRuntimeUnloadResult default_runtime_unload(
      std::string_view plugin_id,
      std::string_view handle_ref);

  [[nodiscard]] ManagedPlugin* find_plugin(std::string_view plugin_id);
  [[nodiscard]] const ManagedPlugin* find_plugin(std::string_view plugin_id) const;

  [[nodiscard]] PluginLoadResult make_load_failure(
      std::string_view plugin_id,
      std::string_view actor_ref,
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string message,
      std::string evidence_ref);
  [[nodiscard]] PluginUnloadResult make_unload_failure(
      std::string_view plugin_id,
      std::string_view actor_ref,
      contracts::ResultCode result_code,
      std::string reason_code,
      std::string message,
      std::string evidence_ref);
  [[nodiscard]] PluginLifecycleTransitionResult make_transition_failure(
      std::string_view plugin_id,
      PluginStatus from_status,
      PluginStatus to_status,
      std::string stage,
      std::string reason_code,
      std::string message,
      std::string evidence_ref) const;

  void record_success();
  void record_failure();
  void emit_load_audit(std::string_view actor_ref,
                       std::string_view plugin_id,
                       bool succeeded,
                       std::string_view evidence_ref,
                       std::string_view reason_code,
                       std::optional<contracts::ResultCode> result_code);
  void emit_unload_audit(std::string_view actor_ref,
                         std::string_view plugin_id,
                         bool succeeded,
                         std::string_view evidence_ref,
                         std::string_view reason_code,
                         std::optional<contracts::ResultCode> result_code);

  std::vector<ManagedPlugin> managed_plugins_;
  PluginRuntimeLoadCallback runtime_load_;
  PluginRuntimeUnloadCallback runtime_unload_;
  PluginAuditAdapter* audit_adapter_;
  std::size_t max_active_;
  std::size_t safe_mode_fail_threshold_;
  std::size_t consecutive_failures_ = 0;
  bool safe_mode_active_ = false;
};

}  // namespace dasall::infra::plugin
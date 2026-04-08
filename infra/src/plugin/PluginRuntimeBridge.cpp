#include "plugin/PluginRuntimeBridge.h"

#include <utility>

#include "plugin/PluginErrorCode.h"

namespace dasall::infra::plugin {
namespace {

[[nodiscard]] std::string load_reason_from_platform_error(
    dasall::platform::PlatformErrorCode error_code) {
  switch (error_code) {
    case dasall::platform::PlatformErrorCode::NotFound:
      return std::string("plugin_runtime_library_open_failed");
    case dasall::platform::PlatformErrorCode::PermissionDenied:
      return std::string("plugin_runtime_library_permission_denied");
    default:
      return std::string("plugin_runtime_library_open_failed");
  }
}

[[nodiscard]] std::string symbol_reason_from_platform_error(
    dasall::platform::PlatformErrorCode error_code) {
  switch (error_code) {
    case dasall::platform::PlatformErrorCode::NotFound:
      return std::string("plugin_runtime_entry_symbol_missing");
    default:
      return std::string("plugin_runtime_symbol_lookup_failed");
  }
}

}  // namespace

PluginRuntimeBridge::PluginRuntimeBridge(
    std::shared_ptr<dasall::platform::IDynamicLibraryLoader> loader)
    : loader_(std::move(loader)) {}

PluginRuntimeLoadResult PluginRuntimeBridge::load(
    const PluginRuntimeLoadRequest& request) {
  if (!request.is_valid()) {
    return make_load_failure(request.plugin_id,
                             std::string("plugin_runtime_invalid_request"),
                             make_evidence_ref("plugin.runtime.load",
                                               request.plugin_id,
                                               "invalid-request"),
                             std::string(
                                 "plugin runtime bridge requires plugin_id, binary_path, entry_symbol and timeout"));
  }

  if (loader_ == nullptr) {
    return make_load_failure(request.plugin_id,
                             std::string("plugin_runtime_loader_unavailable"),
                             make_evidence_ref("plugin.runtime.load",
                                               request.plugin_id,
                                               "loader-missing"),
                             std::string(
                                 "plugin runtime bridge requires a platform dynamic library loader"));
  }

  dasall::platform::DynamicLibraryOpenOptions open_options;
  open_options.resolve_symbols_now = true;
  open_options.export_symbols_globally = false;
  open_options.allow_current_process = false;

  const auto library_result = loader_->open_library(request.binary_path, open_options);
  if (!library_result.ok()) {
    return make_load_failure(request.plugin_id,
                             load_reason_from_platform_error(library_result.error->code),
                             make_evidence_ref("plugin.runtime.load",
                                               request.plugin_id,
                                               "library-open-failed"),
                             library_result.error->detail);
  }

  const auto symbol_result =
      loader_->load_symbol(*library_result.value, request.entry_symbol);
  if (!symbol_result.ok()) {
    static_cast<void>(loader_->close_library(*library_result.value));
    return make_load_failure(request.plugin_id,
                             symbol_reason_from_platform_error(symbol_result.error->code),
                             make_evidence_ref("plugin.runtime.load",
                                               request.plugin_id,
                                               "symbol-lookup-failed"),
                             symbol_result.error->detail);
  }

  return PluginRuntimeLoadResult::success(
      library_result.value->handle_ref,
      make_evidence_ref("plugin.runtime.load", request.plugin_id, "loaded"),
      std::string("plugin_runtime_loaded"));
}

PluginRuntimeUnloadResult PluginRuntimeBridge::unload(
    std::string_view plugin_id,
    std::string_view handle_ref) {
  if (plugin_id.empty() || handle_ref.empty()) {
    return make_unload_failure(
        plugin_id,
        std::string("plugin_runtime_unload_invalid_request"),
        make_evidence_ref("plugin.runtime.unload", plugin_id, "invalid-request"),
        std::string("plugin runtime bridge unload requires plugin_id and handle_ref"));
  }

  if (loader_ == nullptr) {
    return make_unload_failure(
        plugin_id,
        std::string("plugin_runtime_loader_unavailable"),
        make_evidence_ref("plugin.runtime.unload", plugin_id, "loader-missing"),
        std::string("plugin runtime bridge requires a platform dynamic library loader"));
  }

  const auto close_result = loader_->close_library(dasall::platform::DynamicLibraryHandle{
      .handle_ref = std::string(handle_ref),
      .library_path = {},
      .references_current_process = false,
  });
  if (!close_result.ok()) {
    return make_unload_failure(
        plugin_id,
        std::string("plugin_runtime_unload_failed"),
        make_evidence_ref("plugin.runtime.unload", plugin_id, "close-failed"),
        close_result.error->detail);
  }

  return PluginRuntimeUnloadResult::success(
      make_evidence_ref("plugin.runtime.unload", plugin_id, "unloaded"),
      std::string("plugin_runtime_unloaded"));
}

PluginRuntimeLoadResult PluginRuntimeBridge::make_load_failure(
    std::string_view plugin_id,
    std::string reason_code,
    std::string evidence_ref,
    std::string message) const {
  static_cast<void>(plugin_id);
  return PluginRuntimeLoadResult::failure(
      map_plugin_error_code(PluginErrorCode::LoadFail).result_code,
      std::move(reason_code),
      std::move(evidence_ref),
      std::move(message));
}

PluginRuntimeUnloadResult PluginRuntimeBridge::make_unload_failure(
    std::string_view plugin_id,
    std::string reason_code,
    std::string evidence_ref,
    std::string message) const {
  static_cast<void>(plugin_id);
  return PluginRuntimeUnloadResult::failure(
      map_plugin_error_code(PluginErrorCode::UnloadFail).result_code,
      std::move(reason_code),
      std::move(evidence_ref),
      std::move(message));
}

std::string PluginRuntimeBridge::make_evidence_ref(std::string_view action,
                                                   std::string_view plugin_id,
                                                   std::string_view suffix) const {
  return std::string(action) + "." + plugin_value_or_unknown(plugin_id) + "." +
         std::string(suffix);
}

}  // namespace dasall::infra::plugin
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "PluginExtensionBridge.h"
#include "plugin/IPluginManager.h"

namespace dasall::tools::bridge {

class ToolPluginExtensionConsumer;

using ToolPluginExtensionCatalogResolver =
    std::function<std::optional<plugin::ToolPluginExtensionCatalog>(std::string_view plugin_id,
                                                                    std::string_view handle_ref)>;

class ToolPluginLifecycleBridge {
 public:
  ToolPluginLifecycleBridge(std::shared_ptr<infra::plugin::IPluginManager> plugin_manager,
                            std::shared_ptr<PluginExtensionBridge> extension_bridge,
                            ToolPluginExtensionCatalogResolver catalog_resolver,
                            std::shared_ptr<ToolPluginExtensionConsumer> extension_consumer = nullptr);

  [[nodiscard]] std::size_t synchronize_active_plugins();
  [[nodiscard]] bool on_plugin_loaded(const infra::plugin::PluginLoadResult& load_result);
  [[nodiscard]] bool on_plugin_unloaded(const infra::plugin::PluginUnloadResult& unload_result);

 private:
  [[nodiscard]] std::optional<plugin::ToolPluginExtensionCatalog> resolve_catalog(
      std::string_view plugin_id,
      std::string_view handle_ref) const;
  [[nodiscard]] static bool is_visible_plugin_status(infra::plugin::PluginStatus status);
  [[nodiscard]] static bool is_known_plugin_id(std::string_view plugin_id);

  std::shared_ptr<infra::plugin::IPluginManager> plugin_manager_;
  std::shared_ptr<PluginExtensionBridge> extension_bridge_;
  ToolPluginExtensionCatalogResolver catalog_resolver_;
    std::shared_ptr<ToolPluginExtensionConsumer> extension_consumer_;
  std::unordered_set<std::string> tracked_plugin_ids_;
  mutable std::mutex mutex_;
};

}  // namespace dasall::tools::bridge
#include "ToolPluginLifecycleBridge.h"

#include "ToolPluginExtensionConsumer.h"

#include <utility>

namespace dasall::tools::bridge {

ToolPluginLifecycleBridge::ToolPluginLifecycleBridge(
    std::shared_ptr<infra::plugin::IPluginManager> plugin_manager,
    std::shared_ptr<PluginExtensionBridge> extension_bridge,
    ToolPluginExtensionCatalogResolver catalog_resolver,
    std::shared_ptr<ToolPluginExtensionConsumer> extension_consumer)
    : plugin_manager_(std::move(plugin_manager)),
      extension_bridge_(std::move(extension_bridge)),
      catalog_resolver_(std::move(catalog_resolver)),
      extension_consumer_(std::move(extension_consumer)) {}

std::size_t ToolPluginLifecycleBridge::synchronize_active_plugins() {
  if (plugin_manager_ == nullptr || extension_bridge_ == nullptr || !catalog_resolver_) {
    return 0U;
  }

  std::lock_guard<std::mutex> guard(mutex_);

  const auto active_set = plugin_manager_->list_active();
  if (active_set.safe_mode_active) {
    for (const auto& descriptor : active_set.active_plugins) {
      if (is_known_plugin_id(descriptor.plugin_id)) {
        static_cast<void>(extension_bridge_->on_plugin_unloaded(descriptor.plugin_id));
        if (extension_consumer_ != nullptr) {
          static_cast<void>(extension_consumer_->on_plugin_unloaded(descriptor.plugin_id));
        }
      }
    }

    for (const auto& plugin_id : tracked_plugin_ids_) {
      static_cast<void>(extension_bridge_->on_plugin_unloaded(plugin_id));
      if (extension_consumer_ != nullptr) {
        static_cast<void>(extension_consumer_->on_plugin_unloaded(plugin_id));
      }
    }

    tracked_plugin_ids_.clear();
    return 0U;
  }

  std::unordered_set<std::string> next_visible_plugin_ids;
  std::size_t applied = 0U;

  for (const auto& descriptor : active_set.active_plugins) {
    if (!is_known_plugin_id(descriptor.plugin_id)) {
      continue;
    }

    if (!is_visible_plugin_status(descriptor.status)) {
      static_cast<void>(extension_bridge_->on_plugin_unloaded(descriptor.plugin_id));
      if (extension_consumer_ != nullptr) {
        static_cast<void>(extension_consumer_->on_plugin_unloaded(descriptor.plugin_id));
      }
      tracked_plugin_ids_.erase(descriptor.plugin_id);
      continue;
    }

    next_visible_plugin_ids.insert(descriptor.plugin_id);
    const auto catalog = resolve_catalog(descriptor.plugin_id, {});
    const bool bridge_loaded = catalog.has_value() && extension_bridge_->on_plugin_loaded(*catalog);
    const bool consumer_loaded =
        catalog.has_value() &&
        (extension_consumer_ == nullptr || extension_consumer_->on_plugin_loaded(*catalog));
    if (!bridge_loaded || !consumer_loaded) {
      static_cast<void>(extension_bridge_->on_plugin_unloaded(descriptor.plugin_id));
      if (extension_consumer_ != nullptr) {
        static_cast<void>(extension_consumer_->on_plugin_unloaded(descriptor.plugin_id));
      }
      tracked_plugin_ids_.erase(descriptor.plugin_id);
      continue;
    }

    tracked_plugin_ids_.insert(descriptor.plugin_id);
    ++applied;
  }

  for (auto tracked_it = tracked_plugin_ids_.begin(); tracked_it != tracked_plugin_ids_.end();) {
    if (next_visible_plugin_ids.find(*tracked_it) != next_visible_plugin_ids.end()) {
      ++tracked_it;
      continue;
    }

    static_cast<void>(extension_bridge_->on_plugin_unloaded(*tracked_it));
    tracked_it = tracked_plugin_ids_.erase(tracked_it);
  }

  return applied;
}

bool ToolPluginLifecycleBridge::on_plugin_loaded(
    const infra::plugin::PluginLoadResult& load_result) {
  if (extension_bridge_ == nullptr || !catalog_resolver_ || !load_result.loaded ||
      !is_known_plugin_id(load_result.plugin_id) || load_result.handle_ref.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(mutex_);

  const auto catalog = resolve_catalog(load_result.plugin_id, load_result.handle_ref);
  const bool bridge_loaded = catalog.has_value() && extension_bridge_->on_plugin_loaded(*catalog);
  const bool consumer_loaded =
      catalog.has_value() &&
      (extension_consumer_ == nullptr || extension_consumer_->on_plugin_loaded(*catalog));
  if (!bridge_loaded || !consumer_loaded) {
    static_cast<void>(extension_bridge_->on_plugin_unloaded(load_result.plugin_id));
    if (extension_consumer_ != nullptr) {
      static_cast<void>(extension_consumer_->on_plugin_unloaded(load_result.plugin_id));
    }
    tracked_plugin_ids_.erase(load_result.plugin_id);
    return false;
  }

  tracked_plugin_ids_.insert(load_result.plugin_id);
  return true;
}

bool ToolPluginLifecycleBridge::on_plugin_unloaded(
    const infra::plugin::PluginUnloadResult& unload_result) {
  if (extension_bridge_ == nullptr || !unload_result.unloaded ||
      !is_known_plugin_id(unload_result.plugin_id)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(mutex_);

  const bool bridge_changed = extension_bridge_->on_plugin_unloaded(unload_result.plugin_id);
  const bool consumer_changed =
      extension_consumer_ != nullptr && extension_consumer_->on_plugin_unloaded(unload_result.plugin_id);
  const auto erased = tracked_plugin_ids_.erase(unload_result.plugin_id);
  return bridge_changed || consumer_changed || erased > 0U;
}

std::optional<plugin::ToolPluginExtensionCatalog> ToolPluginLifecycleBridge::resolve_catalog(
    std::string_view plugin_id,
    std::string_view handle_ref) const {
  if (!catalog_resolver_ || !is_known_plugin_id(plugin_id)) {
    return std::nullopt;
  }

  return catalog_resolver_(plugin_id, handle_ref);
}

bool ToolPluginLifecycleBridge::is_visible_plugin_status(infra::plugin::PluginStatus status) {
  return status == infra::plugin::PluginStatus::Loaded ||
         status == infra::plugin::PluginStatus::Active;
}

bool ToolPluginLifecycleBridge::is_known_plugin_id(std::string_view plugin_id) {
  return !plugin_id.empty() && plugin_id != infra::plugin::kPluginUnknownValue;
}

}  // namespace dasall::tools::bridge
#include "bridge/ToolPluginExtensionConsumer.h"

#include <utility>

#include "plugin/IPluginManager.h"

namespace dasall::tools::bridge {

ToolPluginExtensionConsumer::ToolPluginExtensionConsumer(
    ToolPluginExtensionConsumerDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

bool ToolPluginExtensionConsumer::on_plugin_loaded(
    const plugin::ToolPluginExtensionCatalog& catalog) {
  const auto delta = PluginExtensionBridge::rebuild_extension_catalog(catalog);
  if (!delta.has_value()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(mutex_);
  if (apply_delta(*delta)) {
    return true;
  }

  static_cast<void>(revoke_source(delta->source_key));
  return false;
}

bool ToolPluginExtensionConsumer::on_plugin_unloaded(std::string_view plugin_id) {
  if (!is_known_plugin_id(plugin_id)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(mutex_);
  return revoke_source(make_source_key(plugin_id));
}

bool ToolPluginExtensionConsumer::apply_delta(const PluginExtensionDelta& delta) {
  if (delta.source_key.empty()) {
    return false;
  }

  return apply_builtin_delta(delta) && apply_mcp_delta(delta) && apply_skill_delta(delta);
}

bool ToolPluginExtensionConsumer::apply_builtin_delta(const PluginExtensionDelta& delta) {
  if (dependencies_.registry == nullptr) {
    return delta.builtin_providers.empty();
  }

  if (delta.builtin_providers.empty()) {
    const auto source_it =
        dependencies_.registry->snapshot()->descriptor_names_by_source.find(delta.source_key);
    if (source_it == dependencies_.registry->snapshot()->descriptor_names_by_source.end()) {
      return true;
    }

    return dependencies_.registry->apply_plugin_extension_delta(delta.source_key, {});
  }

  if (!dependencies_.builtin_descriptor_resolver) {
    return false;
  }

  std::vector<contracts::ToolDescriptor> descriptors;
  for (const auto& provider_view : delta.builtin_providers) {
    const auto resolved_descriptors =
        dependencies_.builtin_descriptor_resolver(provider_view);
    if (!resolved_descriptors.has_value()) {
      return false;
    }

    descriptors.insert(
        descriptors.end(),
        resolved_descriptors->begin(),
        resolved_descriptors->end());
  }

  if (descriptors.empty()) {
    return false;
  }

  return dependencies_.registry->apply_plugin_extension_delta(delta.source_key, descriptors);
}

bool ToolPluginExtensionConsumer::apply_mcp_delta(const PluginExtensionDelta& delta) {
  if (dependencies_.discovery == nullptr) {
    return delta.mcp_launch_specs.empty();
  }

  if (delta.mcp_launch_specs.empty()) {
    const auto source_it =
        dependencies_.discovery->snapshot()->server_ids_by_source.find(delta.source_key);
    if (source_it == dependencies_.discovery->snapshot()->server_ids_by_source.end()) {
      return true;
    }
  }

  return dependencies_.discovery->on_plugin_delta(delta.source_key, delta.mcp_launch_specs);
}

bool ToolPluginExtensionConsumer::apply_skill_delta(const PluginExtensionDelta& delta) {
  if (dependencies_.skill_registry == nullptr) {
    return delta.skill_assets.empty();
  }

  if (delta.skill_assets.empty()) {
    const auto source_it = dependencies_.skill_registry->snapshot()->assets_by_source.find(
        delta.source_key);
    if (source_it == dependencies_.skill_registry->snapshot()->assets_by_source.end()) {
      return true;
    }

    return dependencies_.skill_registry->revoke_source(delta.source_key);
  }

  if (!dependencies_.skill_bundle_importer) {
    return false;
  }

  static_cast<void>(dependencies_.skill_registry->revoke_source(delta.source_key));

  std::vector<skills::SkillSpecAsset> imported_assets;
  for (const auto& skill_asset_ref : delta.skill_assets) {
    const auto import_result = dependencies_.skill_bundle_importer(skill_asset_ref);
    if (has_error_diagnostics(import_result)) {
      return false;
    }

    imported_assets.insert(
        imported_assets.end(),
        import_result.imported_assets.begin(),
        import_result.imported_assets.end());
  }

  for (const auto& asset : imported_assets) {
    if (!dependencies_.skill_registry->register_asset(asset)) {
      return false;
    }
  }

  return true;
}

bool ToolPluginExtensionConsumer::revoke_source(std::string_view source_key) {
  if (source_key.empty()) {
    return false;
  }

  bool changed = false;
  if (dependencies_.discovery != nullptr) {
    changed = dependencies_.discovery->on_plugin_delta(std::string(source_key), {}) || changed;
  }
  if (dependencies_.registry != nullptr) {
    changed = dependencies_.registry->revoke_source(source_key) || changed;
  }
  if (dependencies_.skill_registry != nullptr) {
    changed = dependencies_.skill_registry->revoke_source(source_key) || changed;
  }

  return changed;
}

bool ToolPluginExtensionConsumer::has_error_diagnostics(
    const skills::SkillImportResult& result) {
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.level == skills::SkillImportDiagnosticLevel::Error) {
      return true;
    }
  }

  return false;
}

bool ToolPluginExtensionConsumer::is_known_plugin_id(std::string_view plugin_id) {
  return !plugin_id.empty() && plugin_id != infra::plugin::kPluginUnknownValue;
}

std::string ToolPluginExtensionConsumer::make_source_key(std::string_view plugin_id) {
  return std::string("plugin:") + std::string(plugin_id);
}

}  // namespace dasall::tools::bridge
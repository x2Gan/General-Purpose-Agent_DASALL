#include "PluginExtensionBridge.h"

#include <algorithm>
#include <array>
#include <atomic>

namespace dasall::tools::bridge {

namespace {

template <typename T>
[[nodiscard]] bool has_duplicate_values(const std::vector<T>& values) {
  std::vector<T> sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  return std::adjacent_find(sorted_values.begin(), sorted_values.end()) != sorted_values.end();
}

}  // namespace

std::shared_ptr<const PluginExtensionSnapshot> PluginExtensionBridge::snapshot() const {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

bool PluginExtensionBridge::on_plugin_loaded(
    const plugin::ToolPluginExtensionCatalog& catalog) {
  const auto delta = rebuild_extension_catalog(catalog);
  if (!delta.has_value()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  next_snapshot.builtin_providers_by_source[delta->source_key] = delta->builtin_providers;
  next_snapshot.mcp_launch_specs_by_source[delta->source_key] = delta->mcp_launch_specs;
  next_snapshot.skill_assets_by_source[delta->source_key] = delta->skill_assets;
  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

bool PluginExtensionBridge::on_plugin_unloaded(std::string_view plugin_id) {
  if (plugin_id.empty()) {
    return false;
  }

  const auto source_key = make_source_key(plugin_id);

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  bool changed = false;

  changed = next_snapshot.builtin_providers_by_source.erase(source_key) > 0U || changed;
  changed = next_snapshot.mcp_launch_specs_by_source.erase(source_key) > 0U || changed;
  changed = next_snapshot.skill_assets_by_source.erase(source_key) > 0U || changed;
  if (!changed) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

std::optional<PluginExtensionDelta> PluginExtensionBridge::rebuild_extension_catalog(
    const plugin::ToolPluginExtensionCatalog& catalog) {
  if (catalog.empty() || !payload_kinds_match(catalog)) {
    return std::nullopt;
  }

  const auto plugin_id = resolve_plugin_id(catalog);
  if (!plugin_id.has_value()) {
    return std::nullopt;
  }

  const auto source_key = make_source_key(*plugin_id);
  PluginExtensionDelta delta{
      .source_key = source_key,
      .builtin_providers = emit_builtin_delta(source_key, catalog),
      .mcp_launch_specs = emit_mcp_delta(source_key, catalog),
      .skill_assets = emit_skill_delta(source_key, catalog),
  };

  return delta;
}

std::vector<PluginBuiltinProviderView> PluginExtensionBridge::emit_builtin_delta(
    std::string_view source_key,
    const plugin::ToolPluginExtensionCatalog& catalog) {
  std::vector<PluginBuiltinProviderView> builtin_views;
  builtin_views.reserve(catalog.builtin_tool_providers.size());

  for (const auto& provider : catalog.builtin_tool_providers) {
    builtin_views.push_back(PluginBuiltinProviderView{
        .provider_ref = provider.provider_ref,
        .provider_handle_ref = provider.provider_handle_ref,
        .source_key = std::string(source_key),
    });
  }

  return builtin_views;
}

std::vector<MCPServerLaunchSpec> PluginExtensionBridge::emit_mcp_delta(
    std::string_view source_key,
    const plugin::ToolPluginExtensionCatalog& catalog) {
  std::vector<MCPServerLaunchSpec> launch_specs;
  launch_specs.reserve(catalog.mcp_stdio_servers.size());

  for (const auto& server : catalog.mcp_stdio_servers) {
    launch_specs.push_back(MCPServerLaunchSpec{
        .provider_ref = server.provider_ref,
        .source_key = std::string(source_key),
        .server_id = server.server_id,
        .launch_spec_ref = server.launch_spec_ref,
        .trust_level = server.trust_level,
    });
  }

  return launch_specs;
}

std::vector<SkillAssetRef> PluginExtensionBridge::emit_skill_delta(
    std::string_view source_key,
    const plugin::ToolPluginExtensionCatalog& catalog) {
  std::vector<SkillAssetRef> skill_assets;
  skill_assets.reserve(catalog.skill_bundles.size());

  for (const auto& bundle : catalog.skill_bundles) {
    skill_assets.push_back(SkillAssetRef{
        .provider_ref = bundle.provider_ref,
        .source_key = std::string(source_key),
        .bundle_id = bundle.bundle_id,
        .asset_root_ref = bundle.asset_root_ref,
        .dialect_ref = bundle.dialect_ref,
    });
  }

  return skill_assets;
}

std::optional<std::string> PluginExtensionBridge::resolve_plugin_id(
    const plugin::ToolPluginExtensionCatalog& catalog) {
  std::optional<std::string> plugin_id;

  const auto bind_plugin_id = [&plugin_id](const plugin::ToolPluginProviderRef& provider_ref) {
    if (!provider_ref_is_valid(provider_ref)) {
      return false;
    }

    if (!plugin_id.has_value()) {
      plugin_id = provider_ref.plugin_id;
      return true;
    }

    return *plugin_id == provider_ref.plugin_id;
  };

  for (const auto& provider : catalog.builtin_tool_providers) {
    if (provider.provider_handle_ref.empty() || !bind_plugin_id(provider.provider_ref)) {
      return std::nullopt;
    }
  }

  std::vector<std::string> server_ids;
  server_ids.reserve(catalog.mcp_stdio_servers.size());
  for (const auto& server : catalog.mcp_stdio_servers) {
    if (server.server_id.empty() || server.launch_spec_ref.empty() || server.trust_level.empty() ||
        !bind_plugin_id(server.provider_ref)) {
      return std::nullopt;
    }
    server_ids.push_back(server.server_id);
  }

  if (has_duplicate_values(server_ids)) {
    return std::nullopt;
  }

  std::vector<std::string> bundle_ids;
  bundle_ids.reserve(catalog.skill_bundles.size());
  for (const auto& bundle : catalog.skill_bundles) {
    if (bundle.bundle_id.empty() || bundle.asset_root_ref.empty() ||
        (bundle.dialect_ref.has_value() && bundle.dialect_ref->empty()) ||
        !bind_plugin_id(bundle.provider_ref)) {
      return std::nullopt;
    }
    bundle_ids.push_back(bundle.bundle_id);
  }

  if (has_duplicate_values(bundle_ids)) {
    return std::nullopt;
  }

  if (!plugin_id.has_value()) {
    return std::nullopt;
  }

  return plugin_id;
}

bool PluginExtensionBridge::payload_kinds_match(
    const plugin::ToolPluginExtensionCatalog& catalog) {
  if (has_duplicate_values(catalog.payload_kinds)) {
    return false;
  }

  const auto has_kind = [&catalog](plugin::ToolPluginPayloadKind payload_kind) {
    return std::find(catalog.payload_kinds.begin(), catalog.payload_kinds.end(), payload_kind) !=
           catalog.payload_kinds.end();
  };

  const bool builtin_matches =
      catalog.builtin_tool_providers.empty() == !has_kind(plugin::ToolPluginPayloadKind::builtin_tool_provider);
  const bool mcp_matches =
      catalog.mcp_stdio_servers.empty() == !has_kind(plugin::ToolPluginPayloadKind::mcp_server_stdio);
  const bool skill_matches =
      catalog.skill_bundles.empty() == !has_kind(plugin::ToolPluginPayloadKind::skill_bundle);

  return builtin_matches && mcp_matches && skill_matches;
}

bool PluginExtensionBridge::provider_ref_is_valid(
    const plugin::ToolPluginProviderRef& provider_ref) {
  return !provider_ref.plugin_id.empty() && !provider_ref.export_key.empty() &&
         !provider_ref.source_revision.empty();
}

std::string PluginExtensionBridge::make_source_key(std::string_view plugin_id) {
  return std::string("plugin:") + std::string(plugin_id);
}

void PluginExtensionBridge::publish_snapshot(PluginExtensionSnapshot next_snapshot) {
  std::atomic_store_explicit(
      &snapshot_,
      std::make_shared<const PluginExtensionSnapshot>(std::move(next_snapshot)),
      std::memory_order_release);
}

}  // namespace dasall::tools::bridge
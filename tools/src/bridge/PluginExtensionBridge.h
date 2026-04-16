#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plugin/IToolPluginProvider.h"

namespace dasall::tools::bridge {

struct PluginBuiltinProviderView {
  plugin::ToolPluginProviderRef provider_ref;
  std::string provider_handle_ref;
  std::string source_key;
};

struct MCPServerLaunchSpec {
  plugin::ToolPluginProviderRef provider_ref;
  std::string source_key;
  std::string server_id;
  std::string launch_spec_ref;
  std::string trust_level;
};

struct SkillAssetRef {
  plugin::ToolPluginProviderRef provider_ref;
  std::string source_key;
  std::string bundle_id;
  std::string asset_root_ref;
  std::optional<std::string> dialect_ref;
};

struct PluginExtensionDelta {
  std::string source_key;
  std::vector<PluginBuiltinProviderView> builtin_providers;
  std::vector<MCPServerLaunchSpec> mcp_launch_specs;
  std::vector<SkillAssetRef> skill_assets;
};

struct PluginExtensionSnapshot {
  std::map<std::string, std::vector<PluginBuiltinProviderView>> builtin_providers_by_source;
  std::map<std::string, std::vector<MCPServerLaunchSpec>> mcp_launch_specs_by_source;
  std::map<std::string, std::vector<SkillAssetRef>> skill_assets_by_source;
  std::uint64_t revision = 0U;
};

class PluginExtensionBridge {
 public:
  PluginExtensionBridge() = default;

  [[nodiscard]] std::shared_ptr<const PluginExtensionSnapshot> snapshot() const;
  [[nodiscard]] bool on_plugin_loaded(const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] bool on_plugin_unloaded(std::string_view plugin_id);

  [[nodiscard]] static std::optional<PluginExtensionDelta> rebuild_extension_catalog(
      const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] static std::vector<PluginBuiltinProviderView> emit_builtin_delta(
      std::string_view source_key,
      const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] static std::vector<MCPServerLaunchSpec> emit_mcp_delta(
      std::string_view source_key,
      const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] static std::vector<SkillAssetRef> emit_skill_delta(
      std::string_view source_key,
      const plugin::ToolPluginExtensionCatalog& catalog);

 private:
  [[nodiscard]] static std::optional<std::string> resolve_plugin_id(
      const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] static bool payload_kinds_match(
      const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] static bool provider_ref_is_valid(const plugin::ToolPluginProviderRef& provider_ref);
  [[nodiscard]] static std::string make_source_key(std::string_view plugin_id);
  void publish_snapshot(PluginExtensionSnapshot next_snapshot);

  mutable std::mutex write_mutex_;
  std::shared_ptr<const PluginExtensionSnapshot> snapshot_ =
      std::make_shared<const PluginExtensionSnapshot>();
};

}  // namespace dasall::tools::bridge
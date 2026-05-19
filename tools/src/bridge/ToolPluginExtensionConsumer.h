#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "PluginExtensionBridge.h"
#include "mcp/CapabilityDiscovery.h"
#include "registry/ToolRegistry.h"
#include "skills/ExternalSkillImporter.h"
#include "skills/SkillRegistry.h"

namespace dasall::tools::bridge {

using ToolPluginBuiltinDescriptorResolver =
    std::function<std::optional<std::vector<contracts::ToolDescriptor>>(
        const PluginBuiltinProviderView& provider_view)>;
using ToolPluginSkillBundleImportFn =
    std::function<skills::SkillImportResult(const SkillAssetRef& skill_asset_ref)>;

struct ToolPluginExtensionConsumerDependencies {
  std::shared_ptr<registry::ToolRegistry> registry;
  std::shared_ptr<mcp::CapabilityDiscovery> discovery;
  std::shared_ptr<skills::SkillRegistry> skill_registry;
  ToolPluginBuiltinDescriptorResolver builtin_descriptor_resolver;
  ToolPluginSkillBundleImportFn skill_bundle_importer;
};

class ToolPluginExtensionConsumer {
 public:
  ToolPluginExtensionConsumer() = default;
  explicit ToolPluginExtensionConsumer(ToolPluginExtensionConsumerDependencies dependencies);

  [[nodiscard]] bool on_plugin_loaded(const plugin::ToolPluginExtensionCatalog& catalog);
  [[nodiscard]] bool on_plugin_unloaded(std::string_view plugin_id);

 private:
  [[nodiscard]] bool apply_delta(const PluginExtensionDelta& delta);
  [[nodiscard]] bool apply_builtin_delta(const PluginExtensionDelta& delta);
  [[nodiscard]] bool apply_mcp_delta(const PluginExtensionDelta& delta);
  [[nodiscard]] bool apply_skill_delta(const PluginExtensionDelta& delta);
  [[nodiscard]] bool revoke_source(std::string_view source_key);
  [[nodiscard]] static bool has_error_diagnostics(const skills::SkillImportResult& result);
  [[nodiscard]] static bool is_known_plugin_id(std::string_view plugin_id);
  [[nodiscard]] static std::string make_source_key(std::string_view plugin_id);

  ToolPluginExtensionConsumerDependencies dependencies_;
  mutable std::mutex mutex_;
};

}  // namespace dasall::tools::bridge
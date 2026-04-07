#include "plugin/IPluginManager.h"
#include "plugin/PluginValidationPipeline.h"

namespace dasall::infra::plugin {

namespace {

class PluginManager final : public IPluginManager {
 public:
  [[nodiscard]] PluginCatalog discover(std::string_view profile_id) const override {
    static_cast<void>(profile_id);
    return PluginCatalog{};
  }

  [[nodiscard]] PluginValidationResult validate(
      const PluginValidationRequest& request) const override {
    const PluginValidationPipeline pipeline;
    return pipeline.validate(request);
  }

  PluginLoadResult load(std::string_view plugin_id,
                        const PluginLoadOptions& load_options) override {
    if (plugin_id.empty() || !load_options.is_valid()) {
      return PluginLoadResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string(plugin_id),
          PluginOperationPhase::Load,
          "plugin load requires a plugin_id and valid load options",
          "plugin.load",
          "PluginManager",
          "plugin.load.invalid-request");
    }

    return PluginLoadResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                     std::string(plugin_id),
                                     PluginOperationPhase::Load,
                                     "plugin load skeleton is frozen but not implemented",
                                     "plugin.load",
                                     "PluginManager",
                                     "plugin.load.skeleton");
  }

  PluginUnloadResult unload(std::string_view plugin_id) override {
    if (plugin_id.empty()) {
      return PluginUnloadResult::failure(
          contracts::ResultCode::ValidationFieldMissing,
          std::string(plugin_id),
          "plugin unload requires a plugin_id",
          "plugin.unload",
          "PluginManager",
          "plugin.unload.invalid-request");
    }

    return PluginUnloadResult::failure(contracts::ResultCode::RuntimeRetryExhausted,
                                       std::string(plugin_id),
                                       "plugin unload skeleton is frozen but not implemented",
                                       "plugin.unload",
                                       "PluginManager",
                                       "plugin.unload.skeleton");
  }

  [[nodiscard]] ActivePluginSet list_active() const override {
    return ActivePluginSet{};
  }
};

}  // namespace

}  // namespace dasall::infra::plugin
#include "plugin/IPluginManager.h"
#include "plugin/PluginLifecycleManager.h"
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
    return lifecycle_manager_.load(plugin_id, load_options);
  }

  PluginUnloadResult unload(std::string_view plugin_id) override {
    return lifecycle_manager_.unload(plugin_id);
  }

  [[nodiscard]] ActivePluginSet list_active() const override {
    return lifecycle_manager_.list_active();
  }

 private:
  PluginLifecycleManager lifecycle_manager_;
};

}  // namespace

}  // namespace dasall::infra::plugin
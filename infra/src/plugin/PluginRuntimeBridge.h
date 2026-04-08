#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "IDynamicLibraryLoader.h"
#include "plugin/IPluginRuntimeBridge.h"

namespace dasall::infra::plugin {

class PluginRuntimeBridge final : public IPluginRuntimeBridge {
 public:
  explicit PluginRuntimeBridge(
      std::shared_ptr<dasall::platform::IDynamicLibraryLoader> loader);

  PluginRuntimeLoadResult load(const PluginRuntimeLoadRequest& request) override;
  PluginRuntimeUnloadResult unload(std::string_view plugin_id,
                                   std::string_view handle_ref) override;

 private:
  [[nodiscard]] PluginRuntimeLoadResult make_load_failure(
      std::string_view plugin_id,
      std::string reason_code,
      std::string evidence_ref,
      std::string message) const;
  [[nodiscard]] PluginRuntimeUnloadResult make_unload_failure(
      std::string_view plugin_id,
      std::string reason_code,
      std::string evidence_ref,
      std::string message) const;
  [[nodiscard]] std::string make_evidence_ref(std::string_view action,
                                              std::string_view plugin_id,
                                              std::string_view suffix) const;

  std::shared_ptr<dasall::platform::IDynamicLibraryLoader> loader_;
};

}  // namespace dasall::infra::plugin
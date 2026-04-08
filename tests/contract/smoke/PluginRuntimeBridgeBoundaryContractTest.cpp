#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "plugin/IPluginRuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

template <typename T, typename = void>
struct HasNativeHandle : std::false_type {};

template <typename T>
struct HasNativeHandle<T, std::void_t<decltype(std::declval<T>().native_handle)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasPlatformLoader : std::false_type {};

template <typename T>
struct HasPlatformLoader<T, std::void_t<decltype(std::declval<T>().platform_loader)>>
    : std::true_type {};

class NullPluginRuntimeBridge final : public dasall::infra::plugin::IPluginRuntimeBridge {
 public:
  dasall::infra::plugin::PluginRuntimeLoadResult load(
      const dasall::infra::plugin::PluginRuntimeLoadRequest& request) override {
    if (!request.is_valid()) {
      return dasall::infra::plugin::PluginRuntimeLoadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string("plugin_runtime_invalid_request"),
          std::string("plugin.runtime.load.invalid-request"),
          std::string("plugin runtime request must keep binary path and entry symbol"));
    }

    return dasall::infra::plugin::PluginRuntimeLoadResult::success(
        std::string("dl://plugin.echo"),
        std::string("plugin.runtime.load.plugin.echo.loaded"),
        std::string("plugin_runtime_loaded"));
  }

  dasall::infra::plugin::PluginRuntimeUnloadResult unload(
      std::string_view plugin_id,
      std::string_view handle_ref) override {
    if (plugin_id.empty() || handle_ref.empty()) {
      return dasall::infra::plugin::PluginRuntimeUnloadResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          std::string("plugin_runtime_unload_invalid_request"),
          std::string("plugin.runtime.unload.invalid-request"),
          std::string("plugin runtime unload must keep plugin_id and handle_ref"));
    }

    return dasall::infra::plugin::PluginRuntimeUnloadResult::success(
        std::string("plugin.runtime.unload.plugin.echo.unloaded"),
        std::string("plugin_runtime_unloaded"));
  }
};

void test_plugin_runtime_bridge_keeps_request_and_result_boundary_ref_only() {
  using dasall::infra::plugin::IPluginRuntimeBridge;
  using dasall::infra::plugin::PluginRuntimeLoadRequest;
  using dasall::infra::plugin::PluginRuntimeLoadResult;
  using dasall::infra::plugin::PluginRuntimeUnloadResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(PluginRuntimeLoadRequest{}.plugin_id), std::string>);
  static_assert(std::is_same_v<decltype(PluginRuntimeLoadRequest{}.binary_path), std::string>);
  static_assert(std::is_same_v<decltype(PluginRuntimeLoadRequest{}.entry_symbol), std::string>);
  static_assert(std::is_same_v<decltype(PluginRuntimeLoadRequest{}.sandbox_hint), std::string>);
  static_assert(!HasNativeHandle<PluginRuntimeLoadRequest>::value);
  static_assert(!HasPlatformLoader<PluginRuntimeLoadRequest>::value);
  static_assert(std::is_same_v<decltype(std::declval<IPluginRuntimeBridge&>().load(
                                   std::declval<const PluginRuntimeLoadRequest&>())),
                               PluginRuntimeLoadResult>);
  static_assert(std::is_same_v<decltype(std::declval<IPluginRuntimeBridge&>().unload(
                                   std::declval<std::string_view>(),
                                   std::declval<std::string_view>())),
                               PluginRuntimeUnloadResult>);

  NullPluginRuntimeBridge bridge;
  const auto load_result = bridge.load(PluginRuntimeLoadRequest{
      .plugin_id = std::string("plugin.echo"),
      .binary_path = std::string("./plugins/plugin.echo.so"),
      .entry_symbol = std::string("plugin_entry"),
      .sandbox_hint = std::string("seccomp:basic"),
      .timeout_ms = 3000,
  });
  assert_true(load_result.is_valid() && load_result.loaded,
              "IPluginRuntimeBridge should keep a traceable handle_ref/evidence_ref load boundary without leaking native loader state");

  const auto unload_result = bridge.unload("plugin.echo", load_result.handle_ref);
  assert_true(unload_result.is_valid() && unload_result.unloaded,
              "IPluginRuntimeBridge should keep unload results inside stable contracts result-code semantics");
}

}  // namespace

int main() {
  try {
    test_plugin_runtime_bridge_keeps_request_and_result_boundary_ref_only();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
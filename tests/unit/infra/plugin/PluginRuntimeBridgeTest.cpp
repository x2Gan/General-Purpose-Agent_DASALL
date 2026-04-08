#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "IDynamicLibraryLoader.h"
#include "plugin/PluginLifecycleManager.h"
#include "plugin/PluginRuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::platform::PlatformError make_platform_error(
    dasall::platform::PlatformErrorCode code,
    dasall::platform::PlatformErrorCategory category,
    std::string detail) {
  return dasall::platform::PlatformError{
      .code = code,
      .category = category,
      .retryable_hint = false,
      .syscall_name = {},
      .errno_value = std::nullopt,
      .detail = std::move(detail),
  };
}

[[nodiscard]] dasall::infra::plugin::PluginLoadOptions make_load_options() {
  return dasall::infra::plugin::PluginLoadOptions{
      .profile_id = std::string("desktop_full"),
      .actor_ref = std::string("runtime"),
      .binary_path = std::string("./plugins/plugin.echo.so"),
      .entry_symbol = std::string("plugin_entry"),
      .sandbox_hint = std::string("seccomp:basic"),
      .timeout_ms = 3000,
      .audit_required = true,
      .dry_run = false,
  };
}

class ScriptedDynamicLibraryLoader final : public dasall::platform::IDynamicLibraryLoader {
 public:
  dasall::platform::PlatformResult<dasall::platform::DynamicLibraryHandle> open_library(
      const std::string& library_path,
      const dasall::platform::DynamicLibraryOpenOptions&) override {
    ++open_library_calls;
    last_library_path = library_path;
    if (open_error.has_value()) {
      return dasall::platform::PlatformResult<dasall::platform::DynamicLibraryHandle>::failure(
          *open_error);
    }

    return dasall::platform::PlatformResult<dasall::platform::DynamicLibraryHandle>::success(
        open_handle);
  }

  dasall::platform::PlatformResult<dasall::platform::DynamicLibrarySymbol> load_symbol(
      const dasall::platform::DynamicLibraryHandle& handle,
      const std::string& symbol_name) override {
    ++load_symbol_calls;
    last_handle_ref = handle.handle_ref;
    last_symbol_name = symbol_name;
    if (symbol_error.has_value()) {
      return dasall::platform::PlatformResult<dasall::platform::DynamicLibrarySymbol>::failure(
          *symbol_error);
    }

    return dasall::platform::PlatformResult<dasall::platform::DynamicLibrarySymbol>::success(
        symbol);
  }

  dasall::platform::PlatformResult<bool> close_library(
      const dasall::platform::DynamicLibraryHandle& handle) override {
    ++close_library_calls;
    last_closed_handle_ref = handle.handle_ref;
    if (close_error.has_value()) {
      return dasall::platform::PlatformResult<bool>::failure(*close_error);
    }

    return dasall::platform::PlatformResult<bool>::success(true);
  }

  dasall::platform::DynamicLibraryHandle open_handle{
      .handle_ref = std::string("dl://plugin.echo"),
      .library_path = std::string("./plugins/plugin.echo.so"),
      .references_current_process = false,
  };
  dasall::platform::DynamicLibrarySymbol symbol{
      .symbol_name = std::string("plugin_entry"),
      .address = 0x1000U,
  };
  std::optional<dasall::platform::PlatformError> open_error;
  std::optional<dasall::platform::PlatformError> symbol_error;
  std::optional<dasall::platform::PlatformError> close_error;
  int open_library_calls = 0;
  int load_symbol_calls = 0;
  int close_library_calls = 0;
  std::string last_library_path;
  std::string last_handle_ref;
  std::string last_symbol_name;
  std::string last_closed_handle_ref;
};

void test_plugin_runtime_bridge_maps_platform_loader_success_into_runtime_boundary() {
  using dasall::infra::plugin::PluginLifecycleManager;
  using dasall::infra::plugin::PluginOperationPhase;
  using dasall::infra::plugin::PluginRuntimeBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto loader = std::make_shared<ScriptedDynamicLibraryLoader>();
  PluginRuntimeBridge bridge(loader);
  PluginLifecycleManager manager(bridge);

  const auto load_result = manager.load("plugin.echo", make_load_options());
  assert_true(load_result.loaded &&
                  load_result.phase == PluginOperationPhase::Load &&
                  load_result.handle_ref == "dl://plugin.echo",
              "PluginLifecycleManager should consume IPluginRuntimeBridge and surface the bridge handle_ref");
  assert_equal(1,
               loader->open_library_calls,
               "runtime bridge should open exactly one platform dynamic library on a successful load");
  assert_equal(1,
               loader->load_symbol_calls,
               "runtime bridge should resolve exactly one entry symbol on a successful load");
  assert_true(loader->last_library_path == "./plugins/plugin.echo.so" &&
                  loader->last_symbol_name == "plugin_entry",
              "runtime bridge should forward binary_path and entry_symbol through the platform loader boundary");

  const auto unload_result = manager.unload("plugin.echo");
  assert_true(unload_result.unloaded,
              "PluginLifecycleManager should unload plugins through the runtime bridge boundary");
  assert_equal(1,
               loader->close_library_calls,
               "runtime bridge should close the platform library handle during unload");
  assert_true(loader->last_closed_handle_ref == "dl://plugin.echo",
              "runtime bridge should forward the bridge handle_ref back to the platform loader during unload");
}

void test_plugin_runtime_bridge_closes_platform_handle_when_entry_symbol_lookup_fails() {
  using dasall::infra::plugin::PluginRuntimeBridge;
  using dasall::infra::plugin::PluginRuntimeLoadRequest;
  using dasall::platform::PlatformErrorCategory;
  using dasall::platform::PlatformErrorCode;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto loader = std::make_shared<ScriptedDynamicLibraryLoader>();
  loader->symbol_error = make_platform_error(PlatformErrorCode::NotFound,
                                             PlatformErrorCategory::IO,
                                             std::string("undefined symbol: plugin_entry"));
  PluginRuntimeBridge bridge(loader);

  const auto load_result = bridge.load(PluginRuntimeLoadRequest{
      .plugin_id = std::string("plugin.echo"),
      .binary_path = std::string("./plugins/plugin.echo.so"),
      .entry_symbol = std::string("plugin_entry"),
      .sandbox_hint = std::string("seccomp:basic"),
      .timeout_ms = 3000,
  });

  assert_true(!load_result.loaded && load_result.reason_code == "plugin_runtime_entry_symbol_missing",
              "PluginRuntimeBridge should map missing entry symbols to a stable plugin_runtime reason code");
  assert_equal(1,
               loader->close_library_calls,
               "PluginRuntimeBridge should close the platform library handle when symbol lookup fails after open");
  assert_true(loader->last_closed_handle_ref == "dl://plugin.echo",
              "PluginRuntimeBridge should close the same handle that was returned by the platform loader");
}

}  // namespace

int main() {
  try {
    test_plugin_runtime_bridge_maps_platform_loader_success_into_runtime_boundary();
    test_plugin_runtime_bridge_closes_platform_handle_when_entry_symbol_lookup_fails();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
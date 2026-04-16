#include <atomic>
#include <barrier>
#include <exception>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bridge/PluginExtensionBridge.h"
#include "plugin/IToolPluginProvider.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::plugin::ToolPluginExtensionCatalog make_catalog(
    const std::string& plugin_id,
    int revision) {
  using dasall::tools::plugin::BuiltinToolProviderExport;
  using dasall::tools::plugin::MCPServerStdioExport;
  using dasall::tools::plugin::SkillBundleExport;
  using dasall::tools::plugin::ToolPluginExtensionCatalog;
  using dasall::tools::plugin::ToolPluginPayloadKind;
  using dasall::tools::plugin::ToolPluginProviderRef;

  const auto revision_text = std::string("rev-") + std::to_string(revision);
  const auto suffix = std::string("batch-") + std::to_string(revision);
  return ToolPluginExtensionCatalog{
      .payload_kinds = {
          ToolPluginPayloadKind::builtin_tool_provider,
          ToolPluginPayloadKind::mcp_server_stdio,
          ToolPluginPayloadKind::skill_bundle,
      },
      .builtin_tool_providers = {
          BuiltinToolProviderExport{
              .provider_ref = ToolPluginProviderRef{
                  .plugin_id = plugin_id,
                  .export_key = std::string("builtin.") + suffix,
                  .source_revision = revision_text,
              },
              .provider_handle_ref = std::string("provider://") + plugin_id + "/" + suffix,
          },
      },
      .mcp_stdio_servers = {
          MCPServerStdioExport{
              .provider_ref = ToolPluginProviderRef{
                  .plugin_id = plugin_id,
                  .export_key = std::string("mcp.") + suffix,
                  .source_revision = revision_text,
              },
              .server_id = std::string("server.") + suffix,
              .launch_spec_ref = std::string("launch://") + plugin_id + "/" + suffix,
              .trust_level = std::string("trusted-local"),
          },
      },
      .skill_bundles = {
          SkillBundleExport{
              .provider_ref = ToolPluginProviderRef{
                  .plugin_id = plugin_id,
                  .export_key = std::string("skill.") + suffix,
                  .source_revision = revision_text,
              },
              .bundle_id = std::string("bundle.") + suffix,
              .asset_root_ref = std::string("asset://") + plugin_id + "/" + suffix,
              .dialect_ref = std::string("internal.v1"),
          },
      },
  };
}

void record_failure(std::atomic<bool>& failed,
                    std::mutex& failure_mutex,
                    std::string& failure_message,
                    const std::string& message) {
  if (!failed.exchange(true)) {
    std::lock_guard<std::mutex> guard(failure_mutex);
    failure_message = message;
  }
}

void test_snapshot_reads_remain_consistent_while_delta_writes_serialize() {
  using dasall::tools::bridge::PluginExtensionBridge;

  PluginExtensionBridge bridge;
  constexpr int kReaderCount = 4;
  constexpr int kReadIterations = 200;
  constexpr int kWriteIterations = 120;
  const std::string plugin_id = "plugin.loopback";
  const std::string source_key = "plugin:plugin.loopback";
  std::barrier start_gate(kReaderCount + 1);
  std::atomic<bool> failed = false;
  std::mutex failure_mutex;
  std::string failure_message;
  std::vector<std::thread> readers;
  readers.reserve(kReaderCount);

  for (int reader_index = 0; reader_index < kReaderCount; ++reader_index) {
    readers.emplace_back([&bridge, &start_gate, &failed, &failure_mutex, &failure_message,
                          &source_key, &plugin_id]() {
      start_gate.arrive_and_wait();

      for (int iteration = 0; iteration < kReadIterations && !failed.load(); ++iteration) {
        const auto snapshot = bridge.snapshot();
        const bool has_builtin =
            snapshot->builtin_providers_by_source.find(source_key) !=
            snapshot->builtin_providers_by_source.end();
        const bool has_mcp =
            snapshot->mcp_launch_specs_by_source.find(source_key) !=
            snapshot->mcp_launch_specs_by_source.end();
        const bool has_skill =
            snapshot->skill_assets_by_source.find(source_key) !=
            snapshot->skill_assets_by_source.end();

        if (has_builtin != has_mcp || has_mcp != has_skill) {
          record_failure(failed, failure_mutex, failure_message,
                         "snapshot reads observed a partially published plugin delta");
          return;
        }

        if (has_builtin) {
          const auto& builtin_batch = snapshot->builtin_providers_by_source.at(source_key);
          const auto& mcp_batch = snapshot->mcp_launch_specs_by_source.at(source_key);
          const auto& skill_batch = snapshot->skill_assets_by_source.at(source_key);
          if (builtin_batch.size() != 1U || mcp_batch.size() != 1U || skill_batch.size() != 1U) {
            record_failure(failed, failure_mutex, failure_message,
                           "a published plugin delta batch should remain whole for every source");
            return;
          }

          if (builtin_batch.front().provider_ref.plugin_id != plugin_id ||
              mcp_batch.front().provider_ref.plugin_id != plugin_id ||
              skill_batch.front().provider_ref.plugin_id != plugin_id) {
            record_failure(failed, failure_mutex, failure_message,
                           "a published plugin delta batch should never mix plugin ownership");
            return;
          }
        }
      }
    });
  }

  std::thread writer([&bridge, &start_gate, &failed, &failure_mutex, &failure_message,
                      &plugin_id]() {
    start_gate.arrive_and_wait();

    for (int iteration = 0; iteration < kWriteIterations && !failed.load(); ++iteration) {
      if ((iteration % 2) == 0) {
        if (!bridge.on_plugin_loaded(make_catalog(plugin_id, iteration))) {
          record_failure(failed, failure_mutex, failure_message,
                         "on_plugin_loaded should publish valid plugin batches during concurrent writes");
          return;
        }
      } else if (!bridge.on_plugin_unloaded(plugin_id)) {
        record_failure(failed, failure_mutex, failure_message,
                       "on_plugin_unloaded should revoke the previously published source batch");
        return;
      }
    }
  });

  for (auto& reader : readers) {
    reader.join();
  }
  writer.join();

  if (failed.load()) {
    throw std::runtime_error(failure_message);
  }

  assert_true(bridge.snapshot()->revision >= static_cast<std::uint64_t>(kWriteIterations / 2),
              "serialized plugin writes should keep advancing the bridge snapshot revision");
}

}  // namespace

int main() {
  try {
    test_snapshot_reads_remain_consistent_while_delta_writes_serialize();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
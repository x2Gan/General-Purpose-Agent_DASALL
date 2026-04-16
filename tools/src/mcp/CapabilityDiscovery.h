#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "mcp/CapabilityCache.h"
#include "mcp/StdioMCPServerLauncher.h"
#include "registry/ToolRegistry.h"

namespace dasall::tools::mcp {

struct CapabilityDiscoveryServerRecord {
  std::string source_key;
  bridge::MCPServerLaunchSpec launch_spec;
  MCPServerSpec server_spec;
  std::vector<MCPToolBinding> bindings;
  bool bindings_published = false;
  std::int64_t next_refresh_after_ms = 0;
};

struct CapabilityDiscoveryState {
  std::map<std::string, CapabilityDiscoveryServerRecord> servers_by_id;
  std::map<std::string, std::vector<std::string>> server_ids_by_source;
  std::uint64_t revision = 0U;
};

struct CapabilityDiscoveryRefreshSummary {
  std::vector<std::string> refreshed_server_ids;
  std::vector<std::string> failed_server_ids;
};

struct CapabilityDiscoveryDependencies {
  std::shared_ptr<CapabilityCache> capability_cache;
  std::shared_ptr<IMCPAdapter> adapter;
  std::shared_ptr<registry::ToolRegistry> registry;
  std::shared_ptr<StdioMCPServerLauncher> launcher;
  std::function<std::int64_t()> now_ms;
  std::int64_t refresh_interval_ms = 0;
  std::int64_t failure_backoff_ms = 0;
};

class CapabilityDiscovery {
 public:
  CapabilityDiscovery();
  explicit CapabilityDiscovery(CapabilityDiscoveryDependencies dependencies);

  [[nodiscard]] std::shared_ptr<const CapabilityDiscoveryState> snapshot() const;
  [[nodiscard]] bool on_plugin_delta(
      std::string source_key,
      const std::vector<bridge::MCPServerLaunchSpec>& launch_specs);
  [[nodiscard]] std::vector<MCPServerSpec> schedule_refresh() const;
  [[nodiscard]] CapabilityDiscoveryRefreshSummary refresh_once();
  [[nodiscard]] std::optional<MCPServerSpec> resolve_server_spec(
      std::string_view server_id) const;

 private:
  [[nodiscard]] static CapabilityDiscoveryDependencies default_dependencies();
  [[nodiscard]] std::int64_t current_time_ms() const;
  [[nodiscard]] std::vector<MCPToolBinding> collect_published_bindings_for_source(
      const CapabilityDiscoveryState& state,
      std::string_view source_key) const;
  void publish_snapshot(CapabilityDiscoveryState next_state);

  CapabilityDiscoveryDependencies dependencies_;
  mutable std::mutex write_mutex_;
  std::shared_ptr<const CapabilityDiscoveryState> state_ =
      std::make_shared<const CapabilityDiscoveryState>();
};

}  // namespace dasall::tools::mcp
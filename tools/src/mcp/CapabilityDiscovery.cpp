#include "mcp/CapabilityDiscovery.h"

#include <algorithm>
#include <atomic>
#include <set>
#include <utility>

namespace {

[[nodiscard]] bool contains_string(
    const std::vector<std::string>& values,
    std::string_view probe) {
  return std::find(values.begin(), values.end(), probe) != values.end();
}

}  // namespace

namespace dasall::tools::mcp {

namespace {

struct RefreshOutcome {
  bool success = false;
  std::string server_id;
  std::string source_key;
  std::int64_t next_refresh_after_ms = 0;
  CapabilitySnapshot snapshot;
  std::string error_message;
};

}  // namespace

CapabilityDiscovery::CapabilityDiscovery()
    : CapabilityDiscovery(default_dependencies()) {}

CapabilityDiscovery::CapabilityDiscovery(CapabilityDiscoveryDependencies dependencies)
    : dependencies_(std::move(dependencies)) {
  if (!dependencies_.capability_cache) {
    dependencies_.capability_cache = std::make_shared<CapabilityCache>();
  }
  if (!dependencies_.registry) {
    dependencies_.registry = std::make_shared<registry::ToolRegistry>();
  }
  if (!dependencies_.launcher) {
    dependencies_.launcher = std::make_shared<StdioMCPServerLauncher>();
  }
}

std::shared_ptr<const CapabilityDiscoveryState> CapabilityDiscovery::snapshot() const {
  return std::atomic_load_explicit(&state_, std::memory_order_acquire);
}

bool CapabilityDiscovery::on_plugin_delta(
    std::string source_key,
    const std::vector<bridge::MCPServerLaunchSpec>& launch_specs) {
  if (source_key.empty() || !dependencies_.launcher) {
    return false;
  }

  const auto current_state = snapshot();

  std::vector<CapabilityDiscoveryServerRecord> resolved_records;
  resolved_records.reserve(launch_specs.size());
  std::vector<std::string> next_server_ids;
  next_server_ids.reserve(launch_specs.size());

  for (const auto& launch_spec : launch_specs) {
    const auto server_spec = dependencies_.launcher->build_server_spec(launch_spec);
    if (!server_spec.has_value() || server_spec->server_id.empty() ||
        contains_string(next_server_ids, server_spec->server_id)) {
      return false;
    }

    const auto existing = current_state->servers_by_id.find(server_spec->server_id);
    if (existing != current_state->servers_by_id.end() &&
        existing->second.source_key != source_key) {
      return false;
    }

    next_server_ids.push_back(server_spec->server_id);
    resolved_records.push_back(CapabilityDiscoveryServerRecord{
        .source_key = source_key,
        .launch_spec = launch_spec,
        .server_spec = *server_spec,
        .bindings = dependencies_.launcher->build_bindings(launch_spec),
        .bindings_published = false,
        .next_refresh_after_ms = 0,
    });
  }

  std::vector<std::string> removed_server_ids;
  {
    std::lock_guard<std::mutex> guard(write_mutex_);

    const auto latest_state = snapshot();
    auto next_state = *latest_state;

    const auto owned_servers = next_state.server_ids_by_source.find(source_key);
    if (owned_servers != next_state.server_ids_by_source.end()) {
      removed_server_ids = owned_servers->second;
      for (const auto& server_id : owned_servers->second) {
        next_state.servers_by_id.erase(server_id);
      }
      next_state.server_ids_by_source.erase(owned_servers);
    }

    if (launch_specs.empty()) {
      if (removed_server_ids.empty()) {
        return false;
      }

      next_state.revision = latest_state->revision + 1U;
      publish_snapshot(std::move(next_state));
    } else {
      auto& server_ids = next_state.server_ids_by_source[source_key];
      server_ids.reserve(resolved_records.size());

      for (const auto& record : resolved_records) {
        const auto collision = next_state.servers_by_id.find(record.server_spec.server_id);
        if (collision != next_state.servers_by_id.end() &&
            collision->second.source_key != source_key) {
          return false;
        }

        server_ids.push_back(record.server_spec.server_id);
        next_state.servers_by_id[record.server_spec.server_id] = record;
      }

      next_state.revision = latest_state->revision + 1U;
      publish_snapshot(std::move(next_state));
    }
  }

  if (dependencies_.registry) {
    static_cast<void>(dependencies_.registry->revoke_source(source_key));
  }

  if (dependencies_.capability_cache) {
    for (const auto& server_id : removed_server_ids) {
      if (!contains_string(next_server_ids, server_id)) {
        static_cast<void>(dependencies_.capability_cache->invalidate(server_id));
      }
    }
  }

  return true;
}

std::vector<MCPServerSpec> CapabilityDiscovery::schedule_refresh() const {
  const auto current_state = snapshot();
  const auto now_ms = current_time_ms();

  std::vector<MCPServerSpec> scheduled_specs;
  scheduled_specs.reserve(current_state->servers_by_id.size());

  for (const auto& [server_id, record] : current_state->servers_by_id) {
    static_cast<void>(server_id);
    if (now_ms >= record.next_refresh_after_ms) {
      scheduled_specs.push_back(record.server_spec);
    }
  }

  return scheduled_specs;
}

CapabilityDiscoveryRefreshSummary CapabilityDiscovery::refresh_once() {
  CapabilityDiscoveryRefreshSummary summary;
  const auto current_state = snapshot();
  const auto now_ms = current_time_ms();

  std::vector<RefreshOutcome> outcomes;
  outcomes.reserve(current_state->servers_by_id.size());

  for (const auto& [server_id, record] : current_state->servers_by_id) {
    if (now_ms < record.next_refresh_after_ms) {
      continue;
    }

    RefreshOutcome outcome{};
    outcome.server_id = server_id;
    outcome.source_key = record.source_key;
    outcome.next_refresh_after_ms = now_ms + dependencies_.failure_backoff_ms;

    if (!dependencies_.adapter) {
      outcome.error_message = "mcp.discovery.adapter_unavailable";
      summary.failed_server_ids.push_back(server_id);
      outcomes.push_back(std::move(outcome));
      continue;
    }

    const auto session = dependencies_.adapter->ensure_session(record.server_spec);
    if (!session.ready()) {
      outcome.error_message = "mcp.discovery.session_unavailable";
      summary.failed_server_ids.push_back(server_id);
      outcomes.push_back(std::move(outcome));
      continue;
    }

    auto capability_snapshot = dependencies_.adapter->list_capabilities(session);
    if (capability_snapshot.server_id.empty()) {
      capability_snapshot.server_id = server_id;
    }
    if (!capability_snapshot.trust_marker.has_value() ||
        capability_snapshot.trust_marker->empty()) {
      capability_snapshot.trust_marker = record.server_spec.trust_level;
    }

    if (capability_snapshot.last_error.has_value()) {
      outcome.error_message = *capability_snapshot.last_error;
      summary.failed_server_ids.push_back(server_id);
      outcomes.push_back(std::move(outcome));
      continue;
    }

    outcome.success = true;
    outcome.next_refresh_after_ms = now_ms + dependencies_.refresh_interval_ms;
    outcome.snapshot = std::move(capability_snapshot);
    summary.refreshed_server_ids.push_back(server_id);
    outcomes.push_back(std::move(outcome));
  }

  if (outcomes.empty()) {
    return summary;
  }

  std::vector<RefreshOutcome> applied_outcomes;
  std::set<std::string> publish_sources;
  std::shared_ptr<const CapabilityDiscoveryState> published_state;

  {
    std::lock_guard<std::mutex> guard(write_mutex_);

    const auto latest_state = snapshot();
    auto next_state = *latest_state;
    bool changed = false;

    for (const auto& outcome : outcomes) {
      const auto record_it = next_state.servers_by_id.find(outcome.server_id);
      if (record_it == next_state.servers_by_id.end() ||
          record_it->second.source_key != outcome.source_key) {
        continue;
      }

      record_it->second.next_refresh_after_ms = outcome.next_refresh_after_ms;
      if (outcome.success) {
        record_it->second.bindings_published = true;
        publish_sources.insert(record_it->second.source_key);
      }

      applied_outcomes.push_back(outcome);
      changed = true;
    }

    if (changed) {
      next_state.revision = latest_state->revision + 1U;
      publish_snapshot(std::move(next_state));
      published_state = snapshot();
    }
  }

  for (const auto& outcome : applied_outcomes) {
    if (!dependencies_.capability_cache) {
      continue;
    }

    if (outcome.success) {
      dependencies_.capability_cache->update(outcome.snapshot);
    } else {
      dependencies_.capability_cache->mark_failed(outcome.server_id, outcome.error_message);
    }
  }

  if (dependencies_.registry && published_state) {
    for (const auto& source_key : publish_sources) {
      static_cast<void>(dependencies_.registry->upsert_mcp_bindings(
          source_key,
          collect_published_bindings_for_source(*published_state, source_key)));
    }
  }

  return summary;
}

std::optional<MCPServerSpec> CapabilityDiscovery::resolve_server_spec(
    std::string_view server_id) const {
  if (server_id.empty()) {
    return std::nullopt;
  }

  const auto current_state = snapshot();
  const auto found = current_state->servers_by_id.find(std::string(server_id));
  if (found == current_state->servers_by_id.end()) {
    return std::nullopt;
  }

  return found->second.server_spec;
}

CapabilityDiscoveryDependencies CapabilityDiscovery::default_dependencies() {
  return CapabilityDiscoveryDependencies{
      .capability_cache = std::make_shared<CapabilityCache>(),
      .adapter = {},
      .registry = std::make_shared<registry::ToolRegistry>(),
      .launcher = std::make_shared<StdioMCPServerLauncher>(),
      .now_ms = {},
      .refresh_interval_ms = 0,
      .failure_backoff_ms = 0,
  };
}

std::int64_t CapabilityDiscovery::current_time_ms() const {
  return dependencies_.now_ms ? dependencies_.now_ms() : 0;
}

std::vector<MCPToolBinding> CapabilityDiscovery::collect_published_bindings_for_source(
    const CapabilityDiscoveryState& state,
    std::string_view source_key) const {
  const auto owned_servers = state.server_ids_by_source.find(std::string(source_key));
  if (owned_servers == state.server_ids_by_source.end()) {
    return {};
  }

  std::vector<MCPToolBinding> bindings;
  for (const auto& server_id : owned_servers->second) {
    const auto record_it = state.servers_by_id.find(server_id);
    if (record_it == state.servers_by_id.end() || !record_it->second.bindings_published) {
      continue;
    }

    bindings.insert(
        bindings.end(),
        record_it->second.bindings.begin(),
        record_it->second.bindings.end());
  }

  return bindings;
}

void CapabilityDiscovery::publish_snapshot(CapabilityDiscoveryState next_state) {
  std::atomic_store_explicit(
      &state_,
      std::make_shared<const CapabilityDiscoveryState>(std::move(next_state)),
      std::memory_order_release);
}

}  // namespace dasall::tools::mcp
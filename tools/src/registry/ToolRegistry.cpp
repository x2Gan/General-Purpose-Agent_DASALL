#include "ToolRegistry.h"

#include <algorithm>
#include <atomic>

namespace dasall::tools::registry {

ToolRegistry::ToolRegistry()
    : ToolRegistry(build_builtin_catalog()) {}

ToolRegistry::ToolRegistry(std::vector<contracts::ToolDescriptor> builtin_descriptors) {
  ToolRegistrySnapshot initial_snapshot;

  for (const auto& descriptor : builtin_descriptors) {
    if (upsert_descriptor(initial_snapshot, descriptor, std::string(kBuiltinSourceKey))) {
      initial_snapshot.revision = 1U;
    }
  }

  snapshot_ = std::make_shared<const ToolRegistrySnapshot>(std::move(initial_snapshot));
}

std::shared_ptr<const ToolRegistrySnapshot> ToolRegistry::snapshot() const {
  return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
}

std::optional<contracts::ToolDescriptor> ToolRegistry::resolve_descriptor(
    std::string_view tool_name) const {
  const auto current_snapshot = snapshot();
  const auto descriptor_it = current_snapshot->descriptors_by_name.find(std::string(tool_name));
  if (descriptor_it == current_snapshot->descriptors_by_name.end()) {
    return std::nullopt;
  }

  return descriptor_it->second.descriptor;
}

std::vector<contracts::ToolDescriptor> ToolRegistry::list_descriptors() const {
  const auto current_snapshot = snapshot();
  std::vector<contracts::ToolDescriptor> descriptors;
  descriptors.reserve(current_snapshot->descriptors_by_name.size());

  for (const auto& [tool_name, record] : current_snapshot->descriptors_by_name) {
    static_cast<void>(tool_name);
    descriptors.push_back(record.descriptor);
  }

  return descriptors;
}

std::vector<mcp::MCPToolBinding> ToolRegistry::list_mcp_bindings(
    std::string_view internal_tool_name) const {
  return snapshot()->mcp_binding_registry.list_bindings(internal_tool_name);
}

bool ToolRegistry::register_builtin(const contracts::ToolDescriptor& descriptor) {
  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  if (!upsert_descriptor(next_snapshot, descriptor, std::string(kBuiltinSourceKey))) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

bool ToolRegistry::apply_plugin_extension_delta(
    std::string source_key,
    const std::vector<contracts::ToolDescriptor>& descriptors) {
  if (source_key.empty() || is_builtin_source(source_key)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  bool changed = revoke_descriptors_for_source(next_snapshot, source_key);
  if (!descriptors.empty() && !validate_plugin_descriptor_batch(next_snapshot, descriptors)) {
    return false;
  }

  for (const auto& descriptor : descriptors) {
    if (!upsert_descriptor(next_snapshot, descriptor, source_key)) {
      return false;
    }
    changed = true;
  }

  if (!changed) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

bool ToolRegistry::upsert_mcp_bindings(
    std::string source_key,
    const std::vector<mcp::MCPToolBinding>& bindings) {
  if (bindings.empty()) {
    return revoke_mcp_bindings_for_source(source_key);
  }

  if (source_key.empty() || is_builtin_source(source_key)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  if (!validate_binding_batch(next_snapshot, bindings)) {
    return false;
  }

  if (!next_snapshot.mcp_binding_registry.replace_source_bindings(std::move(source_key), bindings)) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

bool ToolRegistry::revoke_mcp_bindings_for_source(std::string_view source_key) {
  if (source_key.empty() || is_builtin_source(source_key)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  if (!next_snapshot.mcp_binding_registry.revoke_source(source_key)) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

bool ToolRegistry::revoke_source(std::string_view source_key) {
  if (source_key.empty() || is_builtin_source(source_key)) {
    return false;
  }

  std::lock_guard<std::mutex> guard(write_mutex_);

  const auto current_snapshot = snapshot();
  auto next_snapshot = *current_snapshot;
  bool changed = false;
  changed = revoke_descriptors_for_source(next_snapshot, source_key) || changed;
  changed = next_snapshot.mcp_binding_registry.revoke_source(source_key) || changed;
  if (!changed) {
    return false;
  }

  next_snapshot.revision = current_snapshot->revision + 1U;
  publish_snapshot(std::move(next_snapshot));
  return true;
}

bool ToolRegistry::is_builtin_source(std::string_view source_key) {
  return source_key == kBuiltinSourceKey;
}

bool ToolRegistry::validate_binding_batch(
    const ToolRegistrySnapshot& snapshot,
    const std::vector<mcp::MCPToolBinding>& bindings) {
  for (const auto& binding : bindings) {
    if (binding.internal_tool_name.empty() || binding.remote_tool_name.empty() ||
        binding.server_id.empty()) {
      return false;
    }

    if (snapshot.descriptors_by_name.find(binding.internal_tool_name) ==
        snapshot.descriptors_by_name.end()) {
      return false;
    }
  }

  return true;
}

bool ToolRegistry::validate_plugin_descriptor_batch(
    const ToolRegistrySnapshot& snapshot,
    const std::vector<contracts::ToolDescriptor>& descriptors) {
  std::vector<std::string> seen_tool_names;
  seen_tool_names.reserve(descriptors.size());

  for (const auto& descriptor : descriptors) {
    const auto descriptor_guard = contracts::validate_tool_descriptor_field_rules(descriptor);
    if (!descriptor_guard.ok || !descriptor.tool_name.has_value()) {
      return false;
    }

    const auto& tool_name = *descriptor.tool_name;
    if (std::find(seen_tool_names.begin(), seen_tool_names.end(), tool_name) !=
        seen_tool_names.end()) {
      return false;
    }

    if (snapshot.descriptors_by_name.find(tool_name) != snapshot.descriptors_by_name.end()) {
      return false;
    }

    seen_tool_names.push_back(tool_name);
  }

  return true;
}

bool ToolRegistry::upsert_descriptor(
    ToolRegistrySnapshot& snapshot,
    const contracts::ToolDescriptor& descriptor,
    std::string source_key) {
  if (source_key.empty()) {
    return false;
  }

  const auto descriptor_guard = contracts::validate_tool_descriptor_field_rules(descriptor);
  if (!descriptor_guard.ok || !descriptor.tool_name.has_value()) {
    return false;
  }

  const auto& tool_name = *descriptor.tool_name;
  auto existing = snapshot.descriptors_by_name.find(tool_name);
  if (existing != snapshot.descriptors_by_name.end() &&
      existing->second.source_key != source_key) {
    auto source_it = snapshot.descriptor_names_by_source.find(existing->second.source_key);
    if (source_it != snapshot.descriptor_names_by_source.end()) {
      auto& owned_descriptors = source_it->second;
      owned_descriptors.erase(
          std::remove(owned_descriptors.begin(), owned_descriptors.end(), tool_name),
          owned_descriptors.end());
      if (owned_descriptors.empty()) {
        snapshot.descriptor_names_by_source.erase(source_it);
      }
    }
  }

  snapshot.descriptors_by_name[tool_name] = ToolDescriptorRecord{
      .descriptor = descriptor,
      .source_key = source_key,
  };

  auto& owned_descriptors = snapshot.descriptor_names_by_source[source_key];
  if (std::find(owned_descriptors.begin(), owned_descriptors.end(), tool_name) ==
      owned_descriptors.end()) {
    owned_descriptors.push_back(tool_name);
  }

  return true;
}

bool ToolRegistry::revoke_descriptors_for_source(
    ToolRegistrySnapshot& snapshot,
    std::string_view source_key) {
  auto source_it = snapshot.descriptor_names_by_source.find(std::string(source_key));
  if (source_it == snapshot.descriptor_names_by_source.end()) {
    return false;
  }

  for (const auto& tool_name : source_it->second) {
    snapshot.descriptors_by_name.erase(tool_name);
  }

  snapshot.descriptor_names_by_source.erase(source_it);
  return true;
}

void ToolRegistry::publish_snapshot(ToolRegistrySnapshot next_snapshot) {
  std::atomic_store_explicit(
      &snapshot_,
      std::make_shared<const ToolRegistrySnapshot>(std::move(next_snapshot)),
      std::memory_order_release);
}

}  // namespace dasall::tools::registry
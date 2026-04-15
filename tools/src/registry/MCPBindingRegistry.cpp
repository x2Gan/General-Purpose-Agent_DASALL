#include "MCPBindingRegistry.h"

#include <algorithm>

namespace dasall::tools::registry {

std::vector<mcp::MCPToolBinding> MCPBindingRegistry::list_bindings(
    std::string_view internal_tool_name) const {
  std::vector<mcp::MCPToolBinding> bindings;

  for (const auto& [key, record] : bindings_by_key_) {
    static_cast<void>(key);
    if (record.binding.internal_tool_name == internal_tool_name) {
      bindings.push_back(record.binding);
    }
  }

  return bindings;
}

bool MCPBindingRegistry::replace_source_bindings(
    std::string source_key,
    const std::vector<mcp::MCPToolBinding>& bindings) {
  if (source_key.empty()) {
    return false;
  }

  std::set<BindingKey> batch_keys;
  for (const auto& binding : bindings) {
    if (!has_consistent_values(binding)) {
      return false;
    }

    if (!batch_keys.insert(make_key(binding)).second) {
      return false;
    }
  }

  const auto revoked_existing_bindings = revoke_source(source_key);
  static_cast<void>(revoked_existing_bindings);

  for (const auto& binding : bindings) {
    const auto key = make_key(binding);
    auto existing = bindings_by_key_.find(key);
    if (existing != bindings_by_key_.end()) {
      erase_binding_from_source_index(key, existing->second.source_key);
    }

    bindings_by_key_[key] = MCPToolBindingRecord{
        .binding = binding,
        .source_key = source_key,
    };
    binding_keys_by_source_[source_key].push_back(key);
  }

  return true;
}

bool MCPBindingRegistry::revoke_source(std::string_view source_key) {
  if (source_key.empty()) {
    return false;
  }

  auto source_it = binding_keys_by_source_.find(std::string(source_key));
  if (source_it == binding_keys_by_source_.end()) {
    return false;
  }

  for (const auto& key : source_it->second) {
    bindings_by_key_.erase(key);
  }

  binding_keys_by_source_.erase(source_it);
  return true;
}

MCPBindingRegistry::BindingKey MCPBindingRegistry::make_key(
    const mcp::MCPToolBinding& binding) {
  return BindingKey{binding.internal_tool_name, binding.server_id, binding.remote_tool_name};
}

bool MCPBindingRegistry::has_consistent_values(const mcp::MCPToolBinding& binding) {
  return !binding.internal_tool_name.empty() && !binding.remote_tool_name.empty() &&
         !binding.server_id.empty();
}

void MCPBindingRegistry::erase_binding_from_source_index(
    const BindingKey& key,
    std::string_view source_key) {
  auto source_it = binding_keys_by_source_.find(std::string(source_key));
  if (source_it == binding_keys_by_source_.end()) {
    return;
  }

  auto& keys = source_it->second;
  keys.erase(std::remove(keys.begin(), keys.end(), key), keys.end());
  if (keys.empty()) {
    binding_keys_by_source_.erase(source_it);
  }
}

}  // namespace dasall::tools::registry
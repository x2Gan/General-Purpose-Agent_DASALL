#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "BuiltinCatalog.h"
#include "MCPBindingRegistry.h"
#include "tool/ToolDescriptor.h"

namespace dasall::tools::registry {

struct ToolDescriptorRecord {
  contracts::ToolDescriptor descriptor;
  std::string source_key;
};

struct ToolRegistrySnapshot {
  std::map<std::string, ToolDescriptorRecord> descriptors_by_name;
  std::map<std::string, std::vector<std::string>> descriptor_names_by_source;
  MCPBindingRegistry mcp_binding_registry;
  std::uint64_t revision = 0U;
};

class ToolRegistry {
 public:
  ToolRegistry();
  explicit ToolRegistry(std::vector<contracts::ToolDescriptor> builtin_descriptors);

  [[nodiscard]] std::shared_ptr<const ToolRegistrySnapshot> snapshot() const;
  [[nodiscard]] std::optional<contracts::ToolDescriptor> resolve_descriptor(
      std::string_view tool_name) const;
  [[nodiscard]] std::vector<contracts::ToolDescriptor> list_descriptors() const;
  [[nodiscard]] std::vector<mcp::MCPToolBinding> list_mcp_bindings(
      std::string_view internal_tool_name) const;

  [[nodiscard]] bool register_builtin(const contracts::ToolDescriptor& descriptor);
  [[nodiscard]] bool upsert_mcp_bindings(
      std::string source_key,
      const std::vector<mcp::MCPToolBinding>& bindings);
  [[nodiscard]] bool revoke_source(std::string_view source_key);

 private:
  [[nodiscard]] static bool is_builtin_source(std::string_view source_key);
  [[nodiscard]] static bool validate_binding_batch(
      const ToolRegistrySnapshot& snapshot,
      const std::vector<mcp::MCPToolBinding>& bindings);
  [[nodiscard]] static bool upsert_descriptor(
      ToolRegistrySnapshot& snapshot,
      const contracts::ToolDescriptor& descriptor,
      std::string source_key);
  [[nodiscard]] static bool revoke_descriptors_for_source(
      ToolRegistrySnapshot& snapshot,
      std::string_view source_key);
  void publish_snapshot(ToolRegistrySnapshot next_snapshot);

  mutable std::mutex write_mutex_;
  std::shared_ptr<const ToolRegistrySnapshot> snapshot_ =
      std::make_shared<const ToolRegistrySnapshot>();
};

}  // namespace dasall::tools::registry
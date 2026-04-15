#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "mcp/IMCPAdapter.h"

namespace dasall::tools::registry {

struct MCPToolBindingRecord {
  mcp::MCPToolBinding binding;
  std::string source_key;
};

class MCPBindingRegistry {
 public:
  using BindingKey = std::tuple<std::string, std::string, std::string>;

  [[nodiscard]] std::vector<mcp::MCPToolBinding> list_bindings(
      std::string_view internal_tool_name) const;
  [[nodiscard]] bool replace_source_bindings(
      std::string source_key,
      const std::vector<mcp::MCPToolBinding>& bindings);
  [[nodiscard]] bool revoke_source(std::string_view source_key);

 private:
  [[nodiscard]] static BindingKey make_key(const mcp::MCPToolBinding& binding);
  [[nodiscard]] static bool has_consistent_values(const mcp::MCPToolBinding& binding);
  void erase_binding_from_source_index(const BindingKey& key, std::string_view source_key);

  std::map<BindingKey, MCPToolBindingRecord> bindings_by_key_;
  std::map<std::string, std::vector<BindingKey>> binding_keys_by_source_;
};

}  // namespace dasall::tools::registry
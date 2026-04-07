#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "diagnostics/IDiagnosticsCommandRegistry.h"

namespace dasall::infra::diagnostics {

struct CommandRegistryOptions {
  std::string profile_id = "desktop_full";
  std::string catalog_id = "diag-catalog-v1";
  std::string generated_at = "2026-04-07T15:00:00Z";
  std::uint32_t timeout_cap_ms = 3000;
  std::vector<std::string> allowed_commands = {
      std::string(kReadOnlyCommandWhitelist[0]),
      std::string(kReadOnlyCommandWhitelist[1]),
      std::string(kReadOnlyCommandWhitelist[2]),
  };
};

class CommandRegistry final : public IDiagnosticsCommandRegistry {
 public:
  explicit CommandRegistry(CommandRegistryOptions options = {});

  [[nodiscard]] CommandCatalog list_commands() override;
  [[nodiscard]] ValidationResult validate(const DiagnosticsCommand& command) override;

 private:
  [[nodiscard]] bool is_command_enabled(std::string_view command_name) const;
  [[nodiscard]] std::string schema_ref_for(std::string_view command_name) const;
  [[nodiscard]] std::string command_ref_for(std::string_view command_name) const;
  [[nodiscard]] CommandCatalogEntry build_catalog_entry(std::string_view command_name) const;

  CommandRegistryOptions options_{};
  std::unordered_set<std::string> allowed_commands_{};
};

}  // namespace dasall::infra::diagnostics
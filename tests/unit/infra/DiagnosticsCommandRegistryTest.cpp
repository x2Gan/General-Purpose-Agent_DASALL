#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "diagnostics/CommandRegistry.h"
#include "diagnostics/IDiagnosticsCommandRegistry.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::infra::diagnostics::CommandCatalog;
using dasall::infra::diagnostics::CommandRegistry;
using dasall::infra::diagnostics::CommandRegistryOptions;
using dasall::infra::diagnostics::DiagnosticsCommand;
using dasall::infra::diagnostics::IDiagnosticsCommandRegistry;
using dasall::infra::diagnostics::ValidationResult;

[[nodiscard]] DiagnosticsCommand make_command(std::string command_name,
                                              std::vector<std::string> args = {},
                                              std::uint32_t timeout_ms = 3000,
                                              std::string request_scope = "runtime") {
  return DiagnosticsCommand{
      .command_id = std::string("diag-cmd-registry-001"),
      .command_name = std::move(command_name),
      .args = std::move(args),
      .request_scope = std::move(request_scope),
      .timeout_ms = timeout_ms,
      .actor_ref = std::string("ops-user"),
  };
}

[[nodiscard]] bool catalog_contains(const CommandCatalog& catalog, std::string_view command_name) {
  return std::any_of(catalog.entries.begin(), catalog.entries.end(), [&](const auto& entry) {
    return entry.command_name == command_name;
  });
}

void test_command_registry_interface_keeps_frozen_entrypoints() {
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IDiagnosticsCommandRegistry::list_commands),
                               CommandCatalog (IDiagnosticsCommandRegistry::*)()>);
  static_assert(std::is_same_v<decltype(&IDiagnosticsCommandRegistry::validate),
                               ValidationResult (IDiagnosticsCommandRegistry::*)(
                                   const DiagnosticsCommand&)>);
  static_assert(std::is_abstract_v<IDiagnosticsCommandRegistry>);
  static_assert(std::is_same_v<decltype(CommandCatalog{}.entries),
                               std::vector<dasall::infra::diagnostics::CommandCatalogEntry>>);
  static_assert(std::is_same_v<decltype(ValidationResult{}.field_paths), std::vector<std::string>>);

  CommandRegistry registry;
  const auto catalog = registry.list_commands();
  assert_true(catalog.is_valid(),
              "CommandRegistry should keep list_commands constrained to a valid diagnostics read-only catalog");
}

void test_command_registry_catalog_respects_profile_capability_gate() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  CommandRegistry registry(CommandRegistryOptions{
      .profile_id = std::string("edge_balanced"),
      .catalog_id = std::string("diag-catalog-edge"),
      .generated_at = std::string("2026-04-07T16:00:00Z"),
      .timeout_cap_ms = 3000,
      .allowed_commands = {std::string("health.snapshot"), std::string("thread.dump")},
  });

  const auto catalog = registry.list_commands();
  assert_equal(2,
               static_cast<int>(catalog.entries.size()),
               "profile capability gate should prune disabled diagnostics commands from the catalog");
  assert_true(catalog_contains(catalog, "health.snapshot") &&
                  catalog_contains(catalog, "thread.dump") &&
                  !catalog_contains(catalog, "queue.stats"),
              "catalog should expose only enabled diagnostics commands in whitelist order");

  const auto rejection = registry.validate(make_command("queue.stats"));
  assert_true(!rejection.accepted && rejection.is_valid() && rejection.field_paths.size() == 1 &&
                  rejection.field_paths.front() == "command_name",
              "disabled diagnostics commands should fail validation on command_name before schema-specific checks");
}

void test_command_registry_validation_normalizes_empty_args() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  CommandRegistry registry;
  const auto result = registry.validate(make_command("queue.stats"));

  assert_true(result.accepted && result.is_valid(),
              "queue.stats should validate successfully when registry applies the frozen default args");
  assert_equal("schema://diagnostics/queue.stats/v1",
               result.schema_ref,
               "queue.stats should keep the frozen schema ref in validation results");
  assert_equal(1,
               static_cast<int>(result.normalized_command.args.size()),
               "queue.stats normalization should produce a single default token");
  assert_equal("--queue=main",
               result.normalized_command.args.front(),
               "queue.stats normalization should default to the frozen main queue token");
}

void test_command_registry_validation_rejects_invalid_thread_dump_limit() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  CommandRegistry registry;
  const auto result = registry.validate(make_command("thread.dump", {std::string("--limit=64")}));

  assert_true(!result.accepted && result.is_valid() && result.has_blocking_findings(),
              "thread.dump should reject limits outside the frozen v1 range");
  assert_equal("schema://diagnostics/thread.dump/v1",
               result.schema_ref,
               "thread.dump rejections should still point to the matched frozen schema ref");
  assert_true(result.field_paths.size() == 1 && result.field_paths.front() == "args[0]",
              "thread.dump limit failures should remain machine-locatable through args[0]");
}

}  // namespace

int main() {
  try {
    test_command_registry_interface_keeps_frozen_entrypoints();
    test_command_registry_catalog_respects_profile_capability_gate();
    test_command_registry_validation_normalizes_empty_args();
    test_command_registry_validation_rejects_invalid_thread_dump_limit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
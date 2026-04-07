#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "diagnostics/IDiagnosticsCommandRegistry.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class StaticDiagnosticsCommandRegistry final
    : public dasall::infra::diagnostics::IDiagnosticsCommandRegistry {
 public:
  [[nodiscard]] dasall::infra::diagnostics::CommandCatalog list_commands() override {
    return dasall::infra::diagnostics::CommandCatalog{
        .catalog_id = std::string("diag-catalog-001"),
        .profile_id = std::string("desktop_full"),
        .schema_version = std::string(dasall::infra::diagnostics::kDiagnosticsCatalogSchemaVersion),
        .entries = {
            dasall::infra::diagnostics::CommandCatalogEntry{
                .command_name = std::string("health.snapshot"),
                .request_scope = std::string("runtime"),
                .arg_schema_ref = std::string("schema://diagnostics/health.snapshot"),
                .arg_schema_summary = std::string("type=object;required=scope;default=runtime"),
                .read_only = true,
            },
            dasall::infra::diagnostics::CommandCatalogEntry{
                .command_name = std::string("queue.stats"),
                .request_scope = std::string("runtime"),
                .arg_schema_ref = std::string("schema://diagnostics/queue.stats"),
                .arg_schema_summary = std::string("type=object;required=queue;default=main"),
                .read_only = true,
            },
            dasall::infra::diagnostics::CommandCatalogEntry{
                .command_name = std::string("thread.dump"),
                .request_scope = std::string("runtime"),
                .arg_schema_ref = std::string("schema://diagnostics/thread.dump"),
                .arg_schema_summary = std::string("type=object;required=limit;default=5"),
                .read_only = true,
            },
        },
        .generated_at = std::string("2026-04-07T12:00:00Z"),
    };
  }

  [[nodiscard]] dasall::infra::diagnostics::ValidationResult validate(
      const dasall::infra::diagnostics::DiagnosticsCommand& command) override {
    if (!command.has_required_fields()) {
      return dasall::infra::diagnostics::ValidationResult::failure(
          {std::string("diagnostics command metadata is incomplete")},
          {std::string("command_id"), std::string("timeout_ms"), std::string("actor_ref")},
          std::string("diag-catalog-001"));
    }

    if (!command.has_whitelisted_command_name()) {
      return dasall::infra::diagnostics::ValidationResult::failure(
          {std::string("command_name is outside the frozen read-only whitelist")},
          {std::string("command_name")},
          std::string("diag-catalog-001"));
    }

    return dasall::infra::diagnostics::ValidationResult::success(
        std::string("diag-catalog-001"),
        std::string("command://diagnostics/") + command.command_name,
        std::string("schema://diagnostics/") + command.command_name,
        command,
        {std::string("timeout_ms retained from caller input")});
  }
};

void test_command_registry_interface_keeps_frozen_entrypoints() {
  using dasall::infra::diagnostics::CommandCatalog;
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::IDiagnosticsCommandRegistry;
  using dasall::infra::diagnostics::ValidationResult;
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

  StaticDiagnosticsCommandRegistry registry;
  const auto catalog = registry.list_commands();
  assert_true(catalog.is_valid(),
              "IDiagnosticsCommandRegistry should keep list_commands constrained to a valid read-only command catalog");
}

void test_command_registry_catalog_preserves_discoverability_only_fields() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  StaticDiagnosticsCommandRegistry registry;
  const auto catalog = registry.list_commands();

  assert_equal(3,
               static_cast<int>(catalog.entries.size()),
               "diagnostics catalog should keep the frozen three-command read-only surface");
  assert_true(catalog.entries.front().read_only,
              "diagnostics catalog entries should remain read-only in v1");
  assert_true(!catalog.entries.front().arg_schema_ref.empty() &&
                  !catalog.entries.front().arg_schema_summary.empty(),
              "diagnostics catalog entries should expose schema discoverability through ref and summary only");
}

void test_command_registry_validation_results_keep_success_and_failure_boundaries() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::tests::support::assert_true;

  StaticDiagnosticsCommandRegistry registry;

  const auto success = registry.validate(DiagnosticsCommand{
      .command_id = std::string("diag-cmd-registry-001"),
      .command_name = std::string("health.snapshot"),
      .args = {std::string("--summary")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });
  assert_true(success.accepted && success.is_valid() &&
                  success.normalized_command.is_read_only_whitelisted(),
              "registry validation success path should return a normalized read-only command and stable refs");

  const auto failure = registry.validate(DiagnosticsCommand{
      .command_id = std::string("diag-cmd-registry-002"),
      .command_name = std::string("secret.dump"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });
  assert_true(!failure.accepted && failure.is_valid() && failure.has_blocking_findings(),
              "registry validation failure path should remain machine-locatable through blocking_errors and field_paths");
  assert_true(failure.field_paths.size() == 1 && failure.field_paths.front() == "command_name",
              "registry validation failure path should keep stable field path identifiers for locateable rejections");
}

}  // namespace

int main() {
  try {
    test_command_registry_interface_keeps_frozen_entrypoints();
    test_command_registry_catalog_preserves_discoverability_only_fields();
    test_command_registry_validation_results_keep_success_and_failure_boundaries();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
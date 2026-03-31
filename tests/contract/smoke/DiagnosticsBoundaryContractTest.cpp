#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "../../../infra/include/diagnostics/DiagnosticsTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_diagnostics_command_keeps_private_field_types_inside_infra_boundary() {
  using dasall::infra::diagnostics::DiagnosticsCommand;

  static_assert(std::is_same_v<decltype(DiagnosticsCommand{}.command_id), std::string>);
  static_assert(std::is_same_v<decltype(DiagnosticsCommand{}.command_name), std::string>);
  static_assert(std::is_same_v<decltype(DiagnosticsCommand{}.args), std::vector<std::string>>);
  static_assert(std::is_same_v<decltype(DiagnosticsCommand{}.request_scope), std::string>);
  static_assert(std::is_same_v<decltype(DiagnosticsCommand{}.timeout_ms), std::uint32_t>);
  static_assert(std::is_same_v<decltype(DiagnosticsCommand{}.actor_ref), std::string>);
}

void test_diagnostics_command_boundary_keeps_allowlist_frozen() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::kReadOnlyCommandWhitelist;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  constexpr std::array<std::string_view, 3> kExpectedWhitelist{
      "health.snapshot",
      "queue.stats",
      "thread.dump",
  };
  static_assert(kReadOnlyCommandWhitelist == kExpectedWhitelist);

  const DiagnosticsCommand allowed{
      .command_id = std::string("diag-cmd-boundary-001"),
      .command_name = std::string("thread.dump"),
      .args = {std::string("--limit=3")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };
  assert_true(allowed.is_read_only_whitelisted(),
              "diagnostics command boundary should keep the approved read-only command set frozen");

  const DiagnosticsCommand rejected{
      .command_id = std::string("diag-cmd-boundary-002"),
      .command_name = std::string("process.kill"),
      .args = {},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  };
  assert_true(!rejected.is_read_only_whitelisted(),
              "diagnostics boundary should reject commands that are not part of the frozen allowlist");
  assert_equal(static_cast<int>(kExpectedWhitelist.size()),
               static_cast<int>(kReadOnlyCommandWhitelist.size()),
               "diagnostics boundary should keep the allowlist cardinality stable");
}

void test_evidence_bundle_keeps_reference_only_payload_inside_infra_boundary() {
  using dasall::infra::diagnostics::EvidenceBundle;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(EvidenceBundle{}.logs_ref), std::string>);
  static_assert(std::is_same_v<decltype(EvidenceBundle{}.metrics_ref), std::string>);
  static_assert(std::is_same_v<decltype(EvidenceBundle{}.health_ref), std::string>);
  static_assert(std::is_same_v<decltype(EvidenceBundle{}.errors_ref), std::string>);
  static_assert(std::is_same_v<decltype(EvidenceBundle{}.artifacts), std::vector<std::string>>);

  const EvidenceBundle bundle{
      .logs_ref = std::string("logs://diagnostics/boundary"),
      .metrics_ref = std::string("metrics://diagnostics/boundary"),
      .health_ref = std::string("health://diagnostics/boundary"),
      .errors_ref = std::string("errors://diagnostics/boundary"),
      .artifacts = {std::string("artifact://diagnostics/summary.json")},
  };
  assert_true(bundle.is_valid(),
              "diagnostics boundary should keep EvidenceBundle limited to references and lightweight artifact summaries");
}

}  // namespace

int main() {
  try {
    test_diagnostics_command_keeps_private_field_types_inside_infra_boundary();
    test_diagnostics_command_boundary_keeps_allowlist_frozen();
    test_evidence_bundle_keeps_reference_only_payload_inside_infra_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
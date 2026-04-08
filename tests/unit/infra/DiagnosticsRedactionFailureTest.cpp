#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "diagnostics/RedactionEngine.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::diagnostics::DiagnosticsSnapshot make_snapshot(
    dasall::infra::diagnostics::RedactionProfile profile,
    std::vector<std::string> evidence_refs,
    std::string exporter_hint = std::string("local_file")) {
  return dasall::infra::diagnostics::DiagnosticsSnapshot{
      .snapshot_id = std::string("diag-snapshot-redaction-fail-001"),
      .command = dasall::infra::diagnostics::DiagnosticsCommand{
          .command_id = std::string("diag-cmd-redaction-fail-001"),
          .command_name = std::string("health.snapshot"),
          .args = {std::string("--summary")},
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string("ops-user-456"),
      },
      .collected_at = std::string("2026-04-07T21:05:00Z"),
      .summary = std::string("diagnostics health summary"),
      .evidence_refs = std::move(evidence_refs),
      .redaction_profile = profile,
      .exporter_hint = std::move(exporter_hint),
  };
}

void test_redaction_engine_rejects_reference_schemes_outside_the_frozen_allow_list() {
  using dasall::infra::diagnostics::RedactionEngine;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::tests::support::assert_true;

  const auto outcome = RedactionEngine{}.redact(
      make_snapshot(RedactionProfile::Strict, {std::string("raw://diagnostics/inline-secret")}));

  assert_true(!outcome.redacted,
              "redaction should fail when evidence refs carry raw payload schemes outside the frozen allow list");
  assert_true(outcome.error.has_value() &&
                  outcome.error->details.message.find("reference-only evidence refs") != std::string::npos,
              "raw evidence ref failures should surface a stable diagnostics redaction error message");
}

void test_redaction_engine_rejects_non_local_exporter_hint() {
  using dasall::infra::diagnostics::RedactionEngine;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::tests::support::assert_true;

  const auto outcome = RedactionEngine{}.redact(make_snapshot(
      RedactionProfile::Compatibility,
      {std::string("health://diagnostics/health.snapshot")},
      std::string("remote_upload")));

  assert_true(!outcome.redacted,
              "redaction should fail when snapshot exporter hints try to bypass the local-only redaction gate");
  assert_true(outcome.error.has_value() &&
                  outcome.error->details.message.find("local_file") != std::string::npos,
              "non-local exporter hints should surface the fixed local_file-only redaction failure reason");
}

}  // namespace

int main() {
  try {
    test_redaction_engine_rejects_reference_schemes_outside_the_frozen_allow_list();
    test_redaction_engine_rejects_non_local_exporter_hint();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
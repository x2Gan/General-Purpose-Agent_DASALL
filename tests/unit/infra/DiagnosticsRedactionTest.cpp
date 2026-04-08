#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "diagnostics/RedactionEngine.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] bool contains_ref(const std::vector<std::string>& refs, const std::string& expected) {
  return std::find(refs.begin(), refs.end(), expected) != refs.end();
}

dasall::infra::diagnostics::DiagnosticsSnapshot make_snapshot(
    dasall::infra::diagnostics::RedactionProfile profile,
    std::string summary,
    std::vector<std::string> args,
    std::vector<std::string> evidence_refs) {
  return dasall::infra::diagnostics::DiagnosticsSnapshot{
      .snapshot_id = std::string("diag-snapshot-redaction-001"),
      .command = dasall::infra::diagnostics::DiagnosticsCommand{
          .command_id = std::string("diag-cmd-redaction-001"),
          .command_name = std::string("queue.stats"),
          .args = std::move(args),
          .request_scope = std::string("runtime"),
          .timeout_ms = 3000,
          .actor_ref = std::string("ops-user-123"),
      },
      .collected_at = std::string("2026-04-07T21:00:00Z"),
      .summary = std::move(summary),
      .evidence_refs = std::move(evidence_refs),
      .redaction_profile = profile,
      .exporter_hint = std::string("local_file"),
  };
}

void test_redaction_engine_applies_strict_profile_to_actor_args_and_summary() {
  using dasall::infra::diagnostics::RedactionEngine;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::tests::support::assert_true;

  const auto outcome = RedactionEngine{}.redact(make_snapshot(
      RedactionProfile::Strict,
      std::string("diagnostics executor queue stats with token summary"),
      {std::string("--queue=main")},
      {std::string("metrics://diagnostics/queue.stats"),
       std::string("logs://diagnostics/queue.stats")}));

  assert_true(outcome.redacted,
              "strict redaction should succeed for reference-only evidence and whitelisted queue stats input");
  assert_true(outcome.snapshot.command.actor_ref == "actor://redacted",
              "strict redaction should always replace actor_ref with the fixed redacted anchor");
  assert_true(outcome.snapshot.command.args.size() == 1 &&
                  outcome.snapshot.command.args.front() == "--queue=redacted",
              "strict redaction should collapse queue args to the safe redacted token");
  assert_true(outcome.snapshot.summary == "diagnostics redacted queue stats",
              "strict redaction should replace the summary with the canonical queue summary");
}

void test_redaction_engine_keeps_compat_summary_but_masks_deny_tokens() {
  using dasall::infra::diagnostics::RedactionEngine;
  using dasall::infra::diagnostics::RedactionProfile;
  using dasall::tests::support::assert_true;

  const auto outcome = RedactionEngine{}.redact(make_snapshot(
      RedactionProfile::Compatibility,
      std::string("compat token summary for queue stats"),
      {std::string("--queue=token-sensitive")},
      {std::string("metrics://diagnostics/queue.stats"),
       std::string("logs://diagnostics/queue.stats")}));

  assert_true(outcome.redacted,
              "compat redaction should succeed when token-bearing args can be stably rewritten");
  assert_true(outcome.snapshot.command.args.front() == "--queue=redacted",
              "compat redaction should rewrite deny-listed token values while preserving the validated key");
  assert_true(outcome.snapshot.summary.find("[REDACTED]") != std::string::npos &&
                  outcome.snapshot.summary.find("token") == std::string::npos,
              "compat redaction should mask deny-listed summary fragments instead of exposing them verbatim");
  assert_true(contains_ref(outcome.snapshot.evidence_refs, "metrics://diagnostics/queue.stats") &&
                  contains_ref(outcome.snapshot.evidence_refs, "logs://diagnostics/queue.stats"),
              "compat redaction should preserve controlled evidence refs");
}

}  // namespace

int main() {
  try {
    test_redaction_engine_applies_strict_profile_to_actor_args_and_summary();
    test_redaction_engine_keeps_compat_summary_but_masks_deny_tokens();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
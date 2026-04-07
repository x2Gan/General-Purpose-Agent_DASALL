#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "diagnostics/DiagnosticsServiceFacade.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] bool contains_prefix(const std::vector<std::string>& refs, const std::string& prefix) {
  return std::any_of(refs.begin(), refs.end(), [&](const auto& ref) {
    return ref.rfind(prefix, 0) == 0;
  });
}

void test_diagnostics_integration_collects_reference_only_evidence_bundle() {
  using dasall::infra::diagnostics::DiagnosticsCommand;
  using dasall::infra::diagnostics::DiagnosticsServiceFacade;
  using dasall::tests::support::assert_true;

  DiagnosticsServiceFacade service;
  assert_true(service.start(),
              "diagnostics integration path should start the facade before execute");

  const auto execute_result = service.execute(DiagnosticsCommand{
      .command_id = std::string("diag-int-001"),
      .command_name = std::string("queue.stats"),
      .args = {std::string("--queue=main")},
      .request_scope = std::string("runtime"),
      .timeout_ms = 3000,
      .actor_ref = std::string("ops-user"),
  });

  assert_true(execute_result.ok,
              "diagnostics integration path should execute queue.stats through the real facade pipeline");
  assert_true(execute_result.snapshot.is_valid(),
              "diagnostics integration path should produce a valid snapshot after evidence collection");
  assert_true(execute_result.snapshot.evidence_refs.size() >= 5,
              "evidence collection should expand snapshot evidence_refs beyond the executor-only minimum");
  assert_true(contains_prefix(execute_result.snapshot.evidence_refs, "logs://") &&
                  contains_prefix(execute_result.snapshot.evidence_refs, "metrics://") &&
                  contains_prefix(execute_result.snapshot.evidence_refs, "health://") &&
                  contains_prefix(execute_result.snapshot.evidence_refs, "errors://"),
              "evidence collection should preserve logs/metrics/health/errors references as separate traceable summaries");
}

}  // namespace

int main() {
  try {
    test_diagnostics_integration_collects_reference_only_evidence_bundle();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
// WP03-T014-B: AgentResult output-boundary contract tests.
//
// Validates the WP03-T014 AgentResult minimal output semantics and
// boundary guard compliance:
//   - AgentResult struct maps all architecture §5.1 output fields plus
//     §3.8.1 audit references.
//   - AgentResultStatus enum covers completion, failure, degradation,
//     cancellation, and timeout terminal states.
//   - Layer 1 (validate_agent_result_required_fields): 6 required fields.
//   - Layer 2 (validate_agent_result_boundary): inherits L1 + enum range
//     + result_code WP-02 range + optional field boundary constraints.
//
// Test coverage:
//   Positive: 4 scenarios (minimal, full optional, empty response_text,
//             all enum states).
//   Negative: 14 scenarios covering required-field absence, Unspecified
//             status, empty-string violations, result_code out-of-range,
//             optional boundary checks, and tags violations.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "agent/AgentResult.h"
#include "agent/AgentResultGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::AgentResult;
using dasall::contracts::AgentResultGuardResult;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::validate_agent_result_boundary;
using dasall::contracts::validate_agent_result_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal AgentResult with all required fields.
// Uses Completed status with a successful result code.
// ---------------------------------------------------------------------------
AgentResult make_valid_result() {
  AgentResult r;
  r.result_id = "result-014-001";
  r.status = AgentResultStatus::Completed;
  r.result_code = 1001;  // ValidationFieldMissing — within WP-02 range
  r.response_text = "Task completed successfully.";
  r.task_completed = true;
  r.created_at = 1710000014000;
  return r;
}

// ===========================================================================
// Positive cases (4)
// ===========================================================================

// P1: Minimal valid AgentResult — required fields only, L2 passes.
void test_p1_minimal_valid() {
  auto r = make_valid_result();
  auto res = validate_agent_result_boundary(r);
  assert_true(res.ok, "P1: minimal valid result must pass L2 boundary");
}

// P2: Full AgentResult with all optional fields set — L2 passes.
void test_p2_full_optional() {
  auto r = make_valid_result();
  r.request_id = "req-014";
  r.trace_id = "trace-014";
  r.structured_payload = R"({"key":"value"})";
  // error_info is optional and complex — skip in positive minimal test
  r.checkpoint_ref = "ckpt-final-014";
  r.goal_id = "goal-main";
  r.tags = std::vector<std::string>{"result", "audit"};

  auto res = validate_agent_result_boundary(r);
  assert_true(res.ok, "P2: full optional result must pass L2 boundary");
}

// P3: AgentResult with empty response_text — valid (structured output only).
void test_p3_empty_response_text() {
  auto r = make_valid_result();
  r.response_text = "";  // explicitly no text reply

  auto res = validate_agent_result_boundary(r);
  assert_true(res.ok,
              "P3: empty response_text is valid (structured output only)");
}

// P4: All valid AgentResultStatus enum values pass L2.
void test_p4_all_valid_statuses() {
  const AgentResultStatus valid_statuses[] = {
      AgentResultStatus::Completed,
      AgentResultStatus::Failed,
      AgentResultStatus::PartiallyCompleted,
      AgentResultStatus::Cancelled,
      AgentResultStatus::Timeout,
  };

  for (auto s : valid_statuses) {
    auto r = make_valid_result();
    r.status = s;
    auto res = validate_agent_result_boundary(r);
    assert_true(res.ok, "P4: all known non-Unspecified statuses must pass");
  }
}

// ===========================================================================
// Negative cases: required field violations (L1 through L2)
// ===========================================================================

// N1: Missing result_id — L2 rejects (inherited from L1).
void test_n1_missing_result_id() {
  auto r = make_valid_result();
  r.result_id = std::nullopt;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N1: missing result_id must fail");
}

// N2: Empty result_id string — L2 rejects.
void test_n2_empty_result_id() {
  auto r = make_valid_result();
  r.result_id = "";

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N2: empty result_id must fail");
}

// N3: Missing status (nullopt) — L2 rejects.
void test_n3_missing_status() {
  auto r = make_valid_result();
  r.status = std::nullopt;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N3: missing status must fail");
}

// N4: Unspecified status — L2 rejects.
void test_n4_unspecified_status() {
  auto r = make_valid_result();
  r.status = AgentResultStatus::Unspecified;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N4: Unspecified status must fail");
}

// N5: Missing result_code — L2 rejects.
void test_n5_missing_result_code() {
  auto r = make_valid_result();
  r.result_code = std::nullopt;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N5: missing result_code must fail");
}

// N6: result_code out of WP-02 range — L2 rejects.
void test_n6_result_code_out_of_range() {
  auto r = make_valid_result();
  r.result_code = 9999;  // outside 1000-5999

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N6: result_code outside WP-02 range must fail");
}

// N7: Missing response_text (nullopt) — L2 rejects.
void test_n7_missing_response_text() {
  auto r = make_valid_result();
  r.response_text = std::nullopt;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N7: nullopt response_text must fail");
}

// N8: Missing task_completed — L2 rejects.
void test_n8_missing_task_completed() {
  auto r = make_valid_result();
  r.task_completed = std::nullopt;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N8: missing task_completed must fail");
}

// N9: Missing created_at — L2 rejects.
void test_n9_missing_created_at() {
  auto r = make_valid_result();
  r.created_at = std::nullopt;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N9: missing created_at must fail");
}

// N10: Negative created_at — L2 rejects.
void test_n10_negative_created_at() {
  auto r = make_valid_result();
  r.created_at = -100;

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N10: negative created_at must fail");
}

// ===========================================================================
// Negative cases: boundary violations (L2)
// ===========================================================================

// N11: request_id present but empty — L2 rejects.
void test_n11_empty_request_id() {
  auto r = make_valid_result();
  r.request_id = "";

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N11: empty request_id must fail boundary check");
}

// N12: trace_id present but empty — L2 rejects.
void test_n12_empty_trace_id() {
  auto r = make_valid_result();
  r.trace_id = "";

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N12: empty trace_id must fail boundary check");
}

// N13: structured_payload present but empty — L2 rejects.
void test_n13_empty_structured_payload() {
  auto r = make_valid_result();
  r.structured_payload = "";

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok,
              "N13: empty structured_payload must fail boundary check");
}

// N14: tags with empty string — L2 rejects.
void test_n14_tags_contain_empty_string() {
  auto r = make_valid_result();
  r.tags = std::vector<std::string>{"result", "", "audit"};

  auto res = validate_agent_result_boundary(r);
  assert_true(!res.ok, "N14: tags with empty string must fail L2");
}

// ===========================================================================
// Test runner
// ===========================================================================

int run_all_tests() {
  int passed = 0;
  int failed = 0;

  auto run = [&](void (*fn)(), const char* name) {
    try {
      fn();
      ++passed;
    } catch (const std::exception& e) {
      std::cerr << "FAIL [" << name << "]: " << e.what() << "\n";
      ++failed;
    }
  };

  // Positive cases (4).
  run(test_p1_minimal_valid, "P1_minimal_valid");
  run(test_p2_full_optional, "P2_full_optional");
  run(test_p3_empty_response_text, "P3_empty_response_text");
  run(test_p4_all_valid_statuses, "P4_all_valid_statuses");

  // Negative cases (14).
  run(test_n1_missing_result_id, "N1_missing_result_id");
  run(test_n2_empty_result_id, "N2_empty_result_id");
  run(test_n3_missing_status, "N3_missing_status");
  run(test_n4_unspecified_status, "N4_unspecified_status");
  run(test_n5_missing_result_code, "N5_missing_result_code");
  run(test_n6_result_code_out_of_range, "N6_result_code_out_of_range");
  run(test_n7_missing_response_text, "N7_missing_response_text");
  run(test_n8_missing_task_completed, "N8_missing_task_completed");
  run(test_n9_missing_created_at, "N9_missing_created_at");
  run(test_n10_negative_created_at, "N10_negative_created_at");
  run(test_n11_empty_request_id, "N11_empty_request_id");
  run(test_n12_empty_trace_id, "N12_empty_trace_id");
  run(test_n13_empty_structured_payload, "N13_empty_structured_payload");
  run(test_n14_tags_contain_empty_string, "N14_tags_contain_empty_string");

  std::cout << "AgentResultContractTest: " << passed << " passed, "
            << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main() {
  return run_all_tests();
}

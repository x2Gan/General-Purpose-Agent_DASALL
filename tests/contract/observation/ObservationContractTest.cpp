// WP03-T006-B: Observation boundary contract tests.
//
// Validates the WP03-T006 semantic boundary enforced by:
//   - validate_observation_required_fields()  (Layer 1)
//   - validate_observation_boundary()          (Layer 2)
//
// Test coverage:
//   Positive: 4 scenarios proving valid Observation states.
//   Negative: 14 scenarios covering missing required fields, enum range
//             violations, boundary constraint violations, and
//             success/error consistency violations.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "observation/Observation.h"
#include "observation/ObservationGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::Observation;
using dasall::contracts::ObservationGuardResult;
using dasall::contracts::ObservationSource;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::validate_observation_boundary;
using dasall::contracts::validate_observation_required_fields;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid minimal Observation with all required fields set
// and success=true (no error).
// ---------------------------------------------------------------------------
Observation make_valid_success_observation() {
  Observation obs;
  obs.observation_id = "obs-001";
  obs.source = ObservationSource::ToolExecution;
  obs.success = true;
  obs.payload = R"({"result":"file created","path":"/tmp/out.txt"})";
  obs.created_at = 1710000000000;
  return obs;
}

// ---------------------------------------------------------------------------
// Helper: construct a valid failed Observation with error present.
// ---------------------------------------------------------------------------
Observation make_valid_failure_observation() {
  Observation obs;
  obs.observation_id = "obs-002";
  obs.source = ObservationSource::ToolExecution;
  obs.success = false;
  obs.payload = R"({"result":"","error_hint":"permission denied"})";
  obs.created_at = 1710000001000;

  ErrorInfo err;
  err.failure_type = ResultCodeCategory::Tool;
  err.retryable = false;
  err.safe_to_replan = true;
  err.details = ErrorDetails{.code = 403, .message = "permission denied", .stage = "tool_execution"};
  err.source_ref = ErrorSourceRefMinimal{.ref_type = "tool_call", .ref_id = "tc-001"};
  obs.error = err;

  return obs;
}

// ===========================================================================
// Positive cases
// ===========================================================================

// P1: Minimal valid success observation (required fields only).
void test_minimal_success_observation_passes_required_fields() {
  auto obs = make_valid_success_observation();
  auto result = validate_observation_required_fields(obs);
  assert_true(result.ok,
              "minimal success observation should pass required fields guard");
}

// P2: Minimal valid success observation passes boundary guard.
void test_minimal_success_observation_passes_boundary() {
  auto obs = make_valid_success_observation();
  auto result = validate_observation_boundary(obs);
  assert_true(result.ok,
              "minimal success observation should pass boundary guard");
}

// P3: Valid failure observation with error present.
void test_valid_failure_observation_passes_boundary() {
  auto obs = make_valid_failure_observation();
  auto result = validate_observation_boundary(obs);
  assert_true(result.ok,
              "valid failure observation with error should pass boundary");
}

// P4: Full observation with all optional fields set.
void test_full_valid_observation_passes_boundary() {
  auto obs = make_valid_success_observation();
  obs.side_effects = std::vector<std::string>{"created /tmp/out.txt"};
  obs.tool_call_id = "tc-001";
  obs.request_id = "req-001";
  obs.goal_id = "goal-001";
  obs.duration_ms = 1500;
  obs.tags = std::vector<std::string>{"file-io", "write"};

  auto result = validate_observation_boundary(obs);
  assert_true(result.ok,
              "full valid observation with all optional fields should pass");
}

// ===========================================================================
// Negative cases: missing required fields
// ===========================================================================

// N1: Missing observation_id.
void test_missing_observation_id_fails() {
  auto obs = make_valid_success_observation();
  obs.observation_id = std::nullopt;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "missing observation_id should fail");
}

// N2: Empty observation_id.
void test_empty_observation_id_fails() {
  auto obs = make_valid_success_observation();
  obs.observation_id = "";
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "empty observation_id should fail");
}

// N3: Missing source.
void test_missing_source_fails() {
  auto obs = make_valid_success_observation();
  obs.source = std::nullopt;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "missing source should fail");
}

// N4: Unspecified source.
void test_unspecified_source_fails() {
  auto obs = make_valid_success_observation();
  obs.source = ObservationSource::Unspecified;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "Unspecified source should fail");
}

// N5: Missing success.
void test_missing_success_fails() {
  auto obs = make_valid_success_observation();
  obs.success = std::nullopt;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "missing success should fail");
}

// N6: Missing payload.
void test_missing_payload_fails() {
  auto obs = make_valid_success_observation();
  obs.payload = std::nullopt;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "missing payload should fail");
}

// N7: Empty payload.
void test_empty_payload_fails() {
  auto obs = make_valid_success_observation();
  obs.payload = "";
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "empty payload should fail");
}

// N8: Missing created_at.
void test_missing_created_at_fails() {
  auto obs = make_valid_success_observation();
  obs.created_at = std::nullopt;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "missing created_at should fail");
}

// N9: Zero created_at.
void test_zero_created_at_fails() {
  auto obs = make_valid_success_observation();
  obs.created_at = 0;
  auto result = validate_observation_required_fields(obs);
  assert_true(!result.ok, "zero created_at should fail");
}

// ===========================================================================
// Negative cases: boundary violations
// ===========================================================================

// N10: Negative duration_ms.
void test_negative_duration_ms_fails() {
  auto obs = make_valid_success_observation();
  obs.duration_ms = -100;
  auto result = validate_observation_boundary(obs);
  assert_true(!result.ok, "negative duration_ms should fail");
}

// N11: Zero duration_ms.
void test_zero_duration_ms_fails() {
  auto obs = make_valid_success_observation();
  obs.duration_ms = 0;
  auto result = validate_observation_boundary(obs);
  assert_true(!result.ok, "zero duration_ms should fail");
}

// ===========================================================================
// Negative cases: success/error consistency violations
// ===========================================================================

// N12: success=false but error absent.
void test_failure_without_error_fails() {
  auto obs = make_valid_success_observation();
  obs.success = false;
  // error is nullopt — should fail boundary check.
  auto result = validate_observation_boundary(obs);
  assert_true(!result.ok,
              "success=false without error should fail boundary");
}

// N13: success=true but error present.
void test_success_with_error_fails() {
  auto obs = make_valid_failure_observation();
  obs.success = true;
  // error is still present from make_valid_failure_observation.
  auto result = validate_observation_boundary(obs);
  assert_true(!result.ok,
              "success=true with error should fail boundary");
}

// N14: All ObservationSource values (except Unspecified) accepted.
void test_all_valid_sources_accepted() {
  auto obs = make_valid_success_observation();

  obs.source = ObservationSource::ToolExecution;
  assert_true(validate_observation_boundary(obs).ok,
              "ToolExecution source should be accepted");

  obs.source = ObservationSource::WorkerAgent;
  assert_true(validate_observation_boundary(obs).ok,
              "WorkerAgent source should be accepted");

  obs.source = ObservationSource::Retrieval;
  assert_true(validate_observation_boundary(obs).ok,
              "Retrieval source should be accepted");

  obs.source = ObservationSource::HumanFeedback;
  assert_true(validate_observation_boundary(obs).ok,
              "HumanFeedback source should be accepted");
}

}  // namespace

int main() {
  try {
    // Positive cases (4)
    test_minimal_success_observation_passes_required_fields();
    test_minimal_success_observation_passes_boundary();
    test_valid_failure_observation_passes_boundary();
    test_full_valid_observation_passes_boundary();

    // Negative cases: missing required fields (9)
    test_missing_observation_id_fails();
    test_empty_observation_id_fails();
    test_missing_source_fails();
    test_unspecified_source_fails();
    test_missing_success_fails();
    test_missing_payload_fails();
    test_empty_payload_fails();
    test_missing_created_at_fails();
    test_zero_created_at_fails();

    // Negative cases: boundary violations (2)
    test_negative_duration_ms_fails();
    test_zero_duration_ms_fails();

    // Negative cases: success/error consistency (2)
    test_failure_without_error_fails();
    test_success_with_error_fails();

    // Additional positive: all valid sources (1 test, multiple assertions)
    test_all_valid_sources_accepted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

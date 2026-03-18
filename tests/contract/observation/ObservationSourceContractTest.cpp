// WP03-T007-B: ObservationSource correlation contract tests.
//
// Validates the WP03-T007 source→correlation field consistency rules
// enforced by:
//   - validate_observation_source_correlation()  (Layer 3)
//
// Also validates the ObservationSource utility functions:
//   - to_string_view()
//   - is_known_observation_source()
//   - source_to_error_ref_type()
//
// Test coverage:
//   Positive: 5 scenarios proving valid source→correlation combinations.
//   Negative: 14 scenarios covering missing required correlation fields,
//             forbidden correlation fields, empty correlation fields,
//             and Unspecified source rejection.

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "observation/Observation.h"
#include "observation/ObservationGuards.h"
#include "observation/ObservationSource.h"
#include "observation/ObservationSourceGuards.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::Observation;
using dasall::contracts::ObservationSource;
using dasall::contracts::ObservationSourceGuardResult;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::is_known_observation_source;
using dasall::contracts::source_to_error_ref_type;
using dasall::contracts::to_string_view;
using dasall::contracts::validate_observation_source_correlation;
using dasall::tests::support::assert_true;

// ---------------------------------------------------------------------------
// Helper: construct a valid ToolExecution Observation with tool_call_id.
// ---------------------------------------------------------------------------
Observation make_tool_execution_observation() {
  Observation obs;
  obs.observation_id = "obs-tool-001";
  obs.source = ObservationSource::ToolExecution;
  obs.success = true;
  obs.payload = R"({"result":"file created"})";
  obs.created_at = 1710000000000;
  obs.tool_call_id = "tc-001";
  return obs;
}

// ---------------------------------------------------------------------------
// Helper: construct a valid WorkerAgent Observation with worker_task_id.
// ---------------------------------------------------------------------------
Observation make_worker_agent_observation() {
  Observation obs;
  obs.observation_id = "obs-worker-001";
  obs.source = ObservationSource::WorkerAgent;
  obs.success = true;
  obs.payload = R"({"result":"sub-task completed"})";
  obs.created_at = 1710000001000;
  obs.worker_task_id = "wt-001";
  return obs;
}

// ---------------------------------------------------------------------------
// Helper: construct a valid Retrieval Observation (no dedicated correlation).
// ---------------------------------------------------------------------------
Observation make_retrieval_observation() {
  Observation obs;
  obs.observation_id = "obs-retrieval-001";
  obs.source = ObservationSource::Retrieval;
  obs.success = true;
  obs.payload = R"({"result":"3 documents found"})";
  obs.created_at = 1710000002000;
  return obs;
}

// ---------------------------------------------------------------------------
// Helper: construct a valid HumanFeedback Observation (no correlation).
// ---------------------------------------------------------------------------
Observation make_human_feedback_observation() {
  Observation obs;
  obs.observation_id = "obs-human-001";
  obs.source = ObservationSource::HumanFeedback;
  obs.success = true;
  obs.payload = R"({"feedback":"confirmed"})";
  obs.created_at = 1710000003000;
  return obs;
}

// ===========================================================================
// Positive cases: valid source→correlation combinations
// ===========================================================================

// P1: ToolExecution with tool_call_id present.
void test_tool_execution_with_tool_call_id_passes() {
  auto obs = make_tool_execution_observation();
  auto result = validate_observation_source_correlation(obs);
  assert_true(result.ok,
              "ToolExecution with tool_call_id should pass correlation guard");
}

// P2: WorkerAgent with worker_task_id present.
void test_worker_agent_with_worker_task_id_passes() {
  auto obs = make_worker_agent_observation();
  auto result = validate_observation_source_correlation(obs);
  assert_true(result.ok,
              "WorkerAgent with worker_task_id should pass correlation guard");
}

// P3: Retrieval with no correlation fields (minimal valid).
void test_retrieval_minimal_passes() {
  auto obs = make_retrieval_observation();
  auto result = validate_observation_source_correlation(obs);
  assert_true(result.ok,
              "Retrieval with no correlation fields should pass");
}

// P4: HumanFeedback with no correlation fields (minimal valid).
void test_human_feedback_minimal_passes() {
  auto obs = make_human_feedback_observation();
  auto result = validate_observation_source_correlation(obs);
  assert_true(result.ok,
              "HumanFeedback with no correlation fields should pass");
}

// P5: ToolExecution with all optional universal fields (request_id, goal_id).
void test_tool_execution_with_all_optional_fields_passes() {
  auto obs = make_tool_execution_observation();
  obs.request_id = "req-001";
  obs.goal_id = "goal-001";
  auto result = validate_observation_source_correlation(obs);
  assert_true(result.ok,
              "ToolExecution with optional request_id and goal_id should pass");
}

// ===========================================================================
// Negative cases: missing required correlation fields
// ===========================================================================

// N1: ToolExecution without tool_call_id (R1 violation).
void test_tool_execution_missing_tool_call_id_fails() {
  auto obs = make_tool_execution_observation();
  obs.tool_call_id = std::nullopt;
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "ToolExecution without tool_call_id should fail");
}

// N2: WorkerAgent without worker_task_id (R2 violation).
void test_worker_agent_missing_worker_task_id_fails() {
  auto obs = make_worker_agent_observation();
  obs.worker_task_id = std::nullopt;
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "WorkerAgent without worker_task_id should fail");
}

// ===========================================================================
// Negative cases: forbidden correlation fields
// ===========================================================================

// N3: ToolExecution with worker_task_id (R3 violation).
void test_tool_execution_with_worker_task_id_fails() {
  auto obs = make_tool_execution_observation();
  obs.worker_task_id = "wt-forbidden";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "ToolExecution with worker_task_id should fail");
}

// N4: WorkerAgent with tool_call_id (R4 violation).
void test_worker_agent_with_tool_call_id_fails() {
  auto obs = make_worker_agent_observation();
  obs.tool_call_id = "tc-forbidden";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "WorkerAgent with tool_call_id should fail");
}

// N5: HumanFeedback with tool_call_id (R5 violation).
void test_human_feedback_with_tool_call_id_fails() {
  auto obs = make_human_feedback_observation();
  obs.tool_call_id = "tc-forbidden";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "HumanFeedback with tool_call_id should fail");
}

// N6: HumanFeedback with worker_task_id (R6 violation).
void test_human_feedback_with_worker_task_id_fails() {
  auto obs = make_human_feedback_observation();
  obs.worker_task_id = "wt-forbidden";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "HumanFeedback with worker_task_id should fail");
}

// N7: Retrieval with worker_task_id (forbidden).
void test_retrieval_with_worker_task_id_fails() {
  auto obs = make_retrieval_observation();
  obs.worker_task_id = "wt-forbidden";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "Retrieval with worker_task_id should fail");
}

// ===========================================================================
// Negative cases: empty correlation fields (R7 violation)
// ===========================================================================

// N8: ToolExecution with empty tool_call_id.
void test_tool_execution_empty_tool_call_id_fails() {
  auto obs = make_tool_execution_observation();
  obs.tool_call_id = "";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "ToolExecution with empty tool_call_id should fail");
}

// N9: WorkerAgent with empty worker_task_id.
void test_worker_agent_empty_worker_task_id_fails() {
  auto obs = make_worker_agent_observation();
  obs.worker_task_id = "";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "WorkerAgent with empty worker_task_id should fail");
}

// N10: Any source with empty request_id.
void test_empty_request_id_fails() {
  auto obs = make_tool_execution_observation();
  obs.request_id = "";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "Observation with empty request_id should fail");
}

// N11: Any source with empty goal_id.
void test_empty_goal_id_fails() {
  auto obs = make_tool_execution_observation();
  obs.goal_id = "";
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "Observation with empty goal_id should fail");
}

// ===========================================================================
// Negative cases: Unspecified source
// ===========================================================================

// N12: Unspecified source is rejected by correlation guard.
void test_unspecified_source_fails() {
  auto obs = make_tool_execution_observation();
  obs.source = ObservationSource::Unspecified;
  auto result = validate_observation_source_correlation(obs);
  assert_true(!result.ok,
              "Unspecified source should fail correlation guard");
}

// ===========================================================================
// Utility function tests
// ===========================================================================

// N13: to_string_view covers all enum values.
void test_to_string_view_all_values() {
  assert_true(to_string_view(ObservationSource::Unspecified) == "Unspecified",
              "Unspecified to_string_view");
  assert_true(to_string_view(ObservationSource::ToolExecution) == "ToolExecution",
              "ToolExecution to_string_view");
  assert_true(to_string_view(ObservationSource::WorkerAgent) == "WorkerAgent",
              "WorkerAgent to_string_view");
  assert_true(to_string_view(ObservationSource::Retrieval) == "Retrieval",
              "Retrieval to_string_view");
  assert_true(to_string_view(ObservationSource::HumanFeedback) == "HumanFeedback",
              "HumanFeedback to_string_view");
}

// N14: source_to_error_ref_type alignment.
void test_source_to_error_ref_type_alignment() {
  assert_true(source_to_error_ref_type(ObservationSource::ToolExecution) == "tool_call",
              "ToolExecution → tool_call");
  assert_true(source_to_error_ref_type(ObservationSource::WorkerAgent) == "worker_task",
              "WorkerAgent → worker_task");
  assert_true(source_to_error_ref_type(ObservationSource::Retrieval) == "observation",
              "Retrieval → observation");
  assert_true(source_to_error_ref_type(ObservationSource::HumanFeedback) == "observation",
              "HumanFeedback → observation");
  assert_true(source_to_error_ref_type(ObservationSource::Unspecified) == "",
              "Unspecified → empty");
}

// Utility: is_known_observation_source range check.
void test_is_known_observation_source() {
  assert_true(is_known_observation_source(0), "0 is known (Unspecified)");
  assert_true(is_known_observation_source(1), "1 is known (ToolExecution)");
  assert_true(is_known_observation_source(4), "4 is known (HumanFeedback)");
  assert_true(!is_known_observation_source(-1), "-1 is not known");
  assert_true(!is_known_observation_source(5), "5 is not known");
  assert_true(!is_known_observation_source(100), "100 is not known");
}

}  // namespace

int main() {
  try {
    // Positive cases (5)
    test_tool_execution_with_tool_call_id_passes();
    test_worker_agent_with_worker_task_id_passes();
    test_retrieval_minimal_passes();
    test_human_feedback_minimal_passes();
    test_tool_execution_with_all_optional_fields_passes();

    // Negative cases: missing required correlation fields (2)
    test_tool_execution_missing_tool_call_id_fails();
    test_worker_agent_missing_worker_task_id_fails();

    // Negative cases: forbidden correlation fields (5)
    test_tool_execution_with_worker_task_id_fails();
    test_worker_agent_with_tool_call_id_fails();
    test_human_feedback_with_tool_call_id_fails();
    test_human_feedback_with_worker_task_id_fails();
    test_retrieval_with_worker_task_id_fails();

    // Negative cases: empty correlation fields (4)
    test_tool_execution_empty_tool_call_id_fails();
    test_worker_agent_empty_worker_task_id_fails();
    test_empty_request_id_fails();
    test_empty_goal_id_fails();

    // Negative cases: Unspecified source (1)
    test_unspecified_source_fails();

    // Utility function tests (3)
    test_to_string_view_all_values();
    test_source_to_error_ref_type_alignment();
    test_is_known_observation_source();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

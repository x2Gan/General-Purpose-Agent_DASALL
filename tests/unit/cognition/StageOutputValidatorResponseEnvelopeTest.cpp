#include <exception>
#include <iostream>
#include <string>

#include "validation/StageOutputValidator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::ResponseBuildResult;
using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::ValidationIssueCode;
using dasall::contracts::AgentResult;
using dasall::contracts::AgentResultStatus;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] ResponseBuildResult make_valid_response_result() {
  AgentResult agent_result;
  agent_result.result_id = "result-021-response";
  agent_result.status = AgentResultStatus::PartiallyCompleted;
  agent_result.result_code = 0;
  agent_result.response_text = "template fallback response";
  agent_result.task_completed = false;
  agent_result.created_at = 1712746800000LL;
  agent_result.request_id = "req-021-response";
  agent_result.trace_id = "trace-021-response";

  return ResponseBuildResult{
      .result_code = std::nullopt,
      .agent_result = std::move(agent_result),
      .error_info = std::nullopt,
      .fallback_used = true,
      .diagnostics = {},
  };
}

void test_validate_response_envelope_accepts_consistent_fallback_result() {
  StageOutputValidator validator;
  const auto result = validator.validate_response_envelope(make_valid_response_result());

  assert_true(result.ok, "consistent fallback envelopes should pass response validation");
  assert_true(result.issue_set.empty(),
              "consistent fallback envelopes should not emit validation issues");
}

void test_validate_response_envelope_rejects_completed_fallback_without_error_surface() {
  StageOutputValidator validator;
  auto response_result = make_valid_response_result();
  response_result.agent_result->status = AgentResultStatus::Completed;
  response_result.agent_result->task_completed = true;

  const auto result = validator.validate_response_envelope(response_result);

  assert_true(!result.ok, "completed fallback envelopes must fail validation");
  assert_equal(1, static_cast<int>(result.issue_set.issues.size()),
               "completed fallback envelopes should surface one invariant issue");
  assert_true(result.issue_set.issues.front().code == ValidationIssueCode::ResponseEnvelopeInvariant,
              "response envelope failures must use the response-envelope invariant code");
}

}  // namespace

int main() {
  try {
    test_validate_response_envelope_accepts_consistent_fallback_result();
    test_validate_response_envelope_rejects_completed_fallback_without_error_surface();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
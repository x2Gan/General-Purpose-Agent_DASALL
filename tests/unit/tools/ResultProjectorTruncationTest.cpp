#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "projection/ResultProjector.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::tools::route::ToolRouteDecision make_builtin_route() {
  return dasall::tools::route::ToolRouteDecision{
      .available = true,
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .lane_key = std::string("builtin"),
      .reason_code = std::string("route.builtin.selected"),
      .uses_stale_snapshot = false,
      .server_id = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context() {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-projector-truncation"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-projector-truncation"),
          .span_id = std::string("span-projector-truncation"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

void test_result_projector_marks_large_payloads_as_truncated_and_reduces_confidence() {
  const std::string large_payload(5000U, 'x');
  const dasall::contracts::ToolResult result{
      .request_id = std::string("req-projector-truncation"),
      .tool_call_id = std::string("call-projector-truncation"),
      .tool_name = std::string("tool.inspect"),
      .success = true,
      .payload = large_payload,
      .error = std::nullopt,
      .side_effects = std::nullopt,
      .completed_at = 2600,
      .duration_ms = 18,
      .goal_id = std::string("goal-projector-truncation"),
      .worker_task_id = std::string("worker-projector-truncation"),
      .tags = std::vector<std::string>{"builtin"},
  };

  const dasall::tools::projection::ResultProjector projector;
  const auto envelope = projector.project_success(result, make_builtin_route(), make_context());

  assert_true(envelope.observation_digest.has_value(),
              "truncation path should still emit digest");
  assert_true(envelope.observation_digest->summary.has_value() &&
                  envelope.observation_digest->summary->find("[truncated]") != std::string::npos,
              "plain-text payload summaries should be truncated with explicit marker");
  assert_true(envelope.observation_digest->omitted_details.has_value() &&
                  std::find_if(
                      envelope.observation_digest->omitted_details->begin(),
                      envelope.observation_digest->omitted_details->end(),
                      [](const std::string& detail) {
                        return detail.find("payload_bytes=5000") != std::string::npos;
                      }) != envelope.observation_digest->omitted_details->end(),
              "large payload projection should describe omitted byte volume");
  assert_equal(0.75f,
               envelope.observation_digest->confidence.value_or(0.0f),
               "summary truncation plus payload clipping should reduce confidence deterministically");
}

}  // namespace

int main() {
  try {
    test_result_projector_marks_large_payloads_as_truncated_and_reduces_confidence();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
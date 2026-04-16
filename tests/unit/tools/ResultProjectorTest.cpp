#include <algorithm>
#include <exception>
#include <iostream>
#include <string>

#include "observation/ObservationSource.h"
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
      .session_id = std::string("session-projector"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-projector"),
          .span_id = std::string("span-projector"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::nullopt,
  };
}

void test_result_projector_projects_structured_success_without_losing_observation_payload() {
  const dasall::contracts::ToolResult result{
      .request_id = std::string("req-projector"),
      .tool_call_id = std::string("call-projector"),
      .tool_name = std::string("agent.terminal"),
      .success = true,
      .payload = std::string(
          "{\"summary\":\"Shell command completed\",\"file_count\":6,"
          "\"artifacts\":[\"a.txt\",\"b.txt\",\"c.txt\",\"d.txt\",\"e.txt\",\"f.txt\"],"
          "\"details\":{\"cwd\":\"/tmp\"}}"),
      .error = std::nullopt,
      .side_effects = std::nullopt,
      .completed_at = 2400,
      .duration_ms = 14,
      .goal_id = std::string("goal-projector"),
      .worker_task_id = std::string("worker-projector"),
      .tags = std::vector<std::string>{"builtin"},
  };

  const dasall::tools::projection::ResultProjector projector;
  const auto envelope = projector.project_success(result, make_builtin_route(), make_context());

  assert_true(envelope.observation.has_value(),
              "success projection should emit observation");
  assert_equal(static_cast<int>(dasall::contracts::ObservationSource::ToolExecution),
               static_cast<int>(*envelope.observation->source),
               "observation source should remain ToolExecution");
  assert_equal(result.payload.value_or(std::string()),
               envelope.observation->payload.value_or(std::string()),
               "observation should retain raw payload for programmatic consumers");
  assert_true(envelope.observation_digest.has_value(),
              "success projection should emit digest");
  assert_equal(std::string("Shell command completed"),
               envelope.observation_digest->summary.value_or(std::string()),
               "digest should prefer structured summary keys when available");
  assert_true(envelope.observation_digest->key_facts.has_value() &&
                  std::find(envelope.observation_digest->key_facts->begin(),
                            envelope.observation_digest->key_facts->end(),
                            std::string("file_count=6")) !=
                      envelope.observation_digest->key_facts->end(),
              "digest should retain structured scalar payload facts");
  assert_true(envelope.observation_digest->citations.has_value() &&
                  std::find(envelope.observation_digest->citations->begin(),
                            envelope.observation_digest->citations->end(),
                            std::string("tool_call:call-projector")) !=
                      envelope.observation_digest->citations->end(),
              "digest citations should retain tool call identity");
  assert_true(!envelope.observation_digest->omitted_details.has_value(),
              "small structured payload should not emit omitted details");
  assert_equal(1.0f,
               envelope.observation_digest->confidence.value_or(0.0f),
               "complete structured projection should keep full confidence");
}

}  // namespace

int main() {
  try {
    test_result_projector_projects_structured_success_without_losing_observation_payload();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
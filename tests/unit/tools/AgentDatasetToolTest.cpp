#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "builtin/dataset/AgentDatasetTool.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_string(const std::vector<std::string>& values,
                                   const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

[[nodiscard]] dasall::contracts::ToolIR make_tool_ir() {
  return dasall::contracts::ToolIR{
      .request_id = std::string("req-agent-dataset"),
      .tool_call_id = std::string("call-agent-dataset"),
      .tool_name = std::string("agent.dataset"),
      .operation = dasall::contracts::ToolIROperation::Invoke,
      .normalized_arguments = std::string("{\"scope\":\"session\"}"),
      .route = dasall::contracts::ToolIRRoute::LocalTool,
      .timeout_ms = 1800U,
      .idempotency_key = std::string("idem-agent-dataset"),
      .priority = dasall::contracts::ToolIRPriority::Normal,
      .goal_id = std::string("goal-agent-dataset"),
      .worker_task_id = std::string("worker-agent-dataset"),
  };
}

[[nodiscard]] dasall::services::ServiceCallContext make_context() {
  return dasall::services::ServiceCallContext{
      .request_id = std::string("req-agent-dataset"),
      .session_id = std::string("session-agent-dataset"),
      .trace_id = std::string("trace-agent-dataset"),
      .tool_call_id = std::string("call-agent-dataset"),
      .goal_id = std::string("goal-agent-dataset"),
      .budget_guard = std::nullopt,
      .deadline_ms = 1800U,
  };
}

void test_agent_dataset_descriptor_is_read_only_and_schema_bound() {
  const auto descriptor = dasall::tools::builtin::dataset::build_descriptor();

  assert_equal(std::string("agent.dataset"), descriptor.tool_name.value_or(""),
               "agent.dataset wrapper should publish the stable builtin tool name");
  assert_true(descriptor.is_read_only.value_or(false),
              "agent.dataset wrapper should stay read-only for policy and routing");
  assert_true(!descriptor.supports_compensation.value_or(true),
              "agent.dataset wrapper should not advertise compensation for read-only queries");
  assert_equal(std::string("schema://tools/agent.dataset/input/v1"),
               descriptor.input_schema_ref.value_or(""),
               "agent.dataset wrapper should own the builtin input schema ref");
  assert_equal(std::string("schema://tools/agent.dataset/output/v1"),
               descriptor.output_schema_ref.value_or(""),
               "agent.dataset wrapper should own the builtin output schema ref");
  assert_true(descriptor.required_scopes.has_value() &&
                  contains_string(*descriptor.required_scopes, "tools.read"),
              "agent.dataset wrapper should require tools.read scope");
}

void test_agent_dataset_wrapper_maps_query_arguments_without_side_effects() {
  const auto request = dasall::tools::builtin::dataset::build_query_request(
      make_tool_ir(),
      make_context(),
      dasall::services::ServiceDataFreshness::allow_stale);

  assert_equal(std::string("req-agent-dataset"), request.context.request_id,
               "agent.dataset query mapping should preserve request_id");
  assert_equal(std::string("trace-agent-dataset"), request.context.trace_id,
               "agent.dataset query mapping should preserve trace correlation");
  assert_equal(std::string("agent.dataset"), request.dataset,
               "agent.dataset query mapping should preserve the builtin dataset id");
  assert_equal(std::string("{\"scope\":\"session\"}"), request.filters_json,
               "agent.dataset query mapping should forward normalized arguments as filters_json");
  assert_equal(std::string("default"), request.projection,
               "agent.dataset query mapping should keep the default projection until a narrower one is defined");
  assert_equal(static_cast<int>(dasall::services::ServiceDataFreshness::allow_stale),
               static_cast<int>(request.freshness),
               "agent.dataset query mapping should preserve the caller freshness policy");
}

}  // namespace

int main() {
  try {
    test_agent_dataset_descriptor_is_read_only_and_schema_bound();
    test_agent_dataset_wrapper_maps_query_arguments_without_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
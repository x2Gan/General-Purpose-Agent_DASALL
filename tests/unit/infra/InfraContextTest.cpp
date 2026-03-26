#include <exception>
#include <iostream>
#include <string>

#include "InfraContext.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::contracts::AgentRequest make_agent_request(
    std::optional<std::string> request_id,
    std::optional<std::string> session_id,
    std::optional<std::string> trace_id) {
  return dasall::contracts::AgentRequest{
      .request_id = std::move(request_id),
      .session_id = std::move(session_id),
      .trace_id = std::move(trace_id),
      .user_input = std::string("ping"),
      .request_channel = dasall::contracts::RequestChannel::Cli,
      .created_at = 1000,
      .goal_hint = std::nullopt,
      .domain_context = std::nullopt,
      .constraint_set = std::nullopt,
      .approval_policy_hint = std::nullopt,
      .runtime_budget = std::nullopt,
      .timeout_ms = std::nullopt,
      .deadline_at = std::nullopt,
      .priority = std::nullopt,
      .idempotency_key = std::nullopt,
      .locale = std::nullopt,
      .client_capabilities = std::nullopt,
      .tags = std::nullopt,
  };
}

dasall::contracts::WorkerTask make_worker_task(
    std::optional<std::string> task_id,
    std::optional<std::string> parent_task_id,
    std::optional<std::string> lease_id,
    std::string worker_type) {
  return dasall::contracts::WorkerTask{
      .task_id = std::move(task_id),
      .parent_task_id = std::move(parent_task_id),
      .lease_id = std::move(lease_id),
      .worker_type = std::move(worker_type),
      .allowed_tools = std::nullopt,
      .timeout_ms = std::nullopt,
      .idempotency_key = std::nullopt,
  };
}

dasall::contracts::WorkerLease make_worker_lease(std::optional<std::string> lease_id) {
  return dasall::contracts::WorkerLease{
      .lease_id = std::move(lease_id),
      .worker_ref = std::string("worker-A"),
      .deadline_at = 2000,
      .renewal_deadline_at = std::nullopt,
      .release_reason = std::nullopt,
  };
}

void test_default_context_uses_unknown_identifiers() {
  using dasall::infra::InfraContext;
  using dasall::tests::support::assert_equal;

  const InfraContext context;

  assert_equal("unknown", context.request_id, "default request_id should be unknown");
  assert_equal("unknown", context.session_id, "default session_id should be unknown");
  assert_equal("unknown", context.trace_id, "default trace_id should be unknown");
  assert_equal("unknown", context.task_id, "default task_id should be unknown");
  assert_equal("unknown", context.parent_task_id,
               "default parent_task_id should be unknown");
  assert_equal("unknown", context.lease_id, "default lease_id should be unknown");
}

void test_from_contracts_maps_request_task_and_lease_identifiers() {
  using dasall::infra::InfraContext;
  using dasall::tests::support::assert_equal;

  const auto request = make_agent_request("req-001", "sess-001", "trace-001");
  const auto task = make_worker_task("task-001", "task-parent-001", "lease-task-001", "planner");
  const auto lease = make_worker_lease("lease-001");

  const auto context = InfraContext::from_contracts(request, &task, &lease);

  assert_equal("req-001", context.request_id, "request_id should map from AgentRequest");
  assert_equal("sess-001", context.session_id, "session_id should map from AgentRequest");
  assert_equal("trace-001", context.trace_id, "trace_id should map from AgentRequest");
  assert_equal("task-001", context.task_id, "task_id should map from WorkerTask");
  assert_equal("task-parent-001", context.parent_task_id,
               "parent_task_id should map from WorkerTask");
  assert_equal("lease-task-001", context.lease_id,
               "WorkerTask lease_id should take precedence when present");
}

void test_from_contracts_normalizes_missing_or_empty_identifiers_to_unknown() {
  using dasall::infra::InfraContext;
  using dasall::tests::support::assert_equal;

  const auto request = make_agent_request(std::string(), std::nullopt, std::string());
  const auto task = make_worker_task(std::nullopt, std::string(), std::string(), "planner");
  const auto lease = make_worker_lease("lease-fallback-001");

  const auto context = InfraContext::from_contracts(request, &task, &lease);

  assert_equal("unknown", context.request_id,
               "empty request_id should normalize to unknown");
  assert_equal("unknown", context.session_id,
               "missing session_id should normalize to unknown");
  assert_equal("unknown", context.trace_id,
               "empty trace_id should normalize to unknown");
  assert_equal("unknown", context.task_id, "missing task_id should normalize to unknown");
  assert_equal("unknown", context.parent_task_id,
               "empty parent_task_id should normalize to unknown");
  assert_equal("lease-fallback-001", context.lease_id,
               "WorkerLease lease_id should backfill unknown lease anchors");
}

}  // namespace

int main() {
  try {
    test_default_context_uses_unknown_identifiers();
    test_from_contracts_maps_request_task_and_lease_identifiers();
    test_from_contracts_normalizes_missing_or_empty_identifiers_to_unknown();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <string>

#include "agent/AgentRequestGuards.h"
#include "task/WorkerLease.h"
#include "task/WorkerTask.h"
#include "../../../infra/include/InfraContext.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::contracts::AgentRequest make_agent_request(
    std::optional<std::string> request_id,
    std::optional<std::string> session_id,
    std::optional<std::string> trace_id,
    dasall::contracts::RequestChannel request_channel) {
  return dasall::contracts::AgentRequest{
      .request_id = std::move(request_id),
      .session_id = std::move(session_id),
      .trace_id = std::move(trace_id),
      .user_input = std::string("ping"),
      .request_channel = request_channel,
      .created_at = 2000,
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
    std::optional<std::string> lease_id) {
  return dasall::contracts::WorkerTask{
      .task_id = std::move(task_id),
      .parent_task_id = std::move(parent_task_id),
      .lease_id = std::move(lease_id),
      .worker_type = std::string("executor"),
      .allowed_tools = std::nullopt,
      .timeout_ms = std::nullopt,
      .idempotency_key = std::nullopt,
  };
}

dasall::contracts::WorkerLease make_worker_lease(std::optional<std::string> lease_id) {
  return dasall::contracts::WorkerLease{
      .lease_id = std::move(lease_id),
      .worker_ref = std::string("worker-contract-001"),
      .deadline_at = 3000,
      .renewal_deadline_at = std::nullopt,
      .release_reason = std::nullopt,
  };
}

void test_infra_context_consumes_only_existing_contract_identifiers() {
  using dasall::infra::InfraContext;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto request = make_agent_request(
      "req-contract-001", "sess-contract-001", "trace-contract-001",
      dasall::contracts::RequestChannel::Gateway);
  const auto request_guard = validate_agent_request_boundary(request);
  assert_true(request_guard.ok, "AgentRequest boundary should pass before InfraContext mapping");

  const auto task = make_worker_task(
      "task-contract-001", "task-parent-contract-001", "lease-from-task-001");
  const auto lease = make_worker_lease("lease-from-lease-001");

  const auto context = InfraContext::from_contracts(request, &task, &lease);

  assert_equal("req-contract-001", context.request_id,
               "InfraContext should preserve AgentRequest request_id semantics");
  assert_equal("sess-contract-001", context.session_id,
               "InfraContext should preserve AgentRequest session_id semantics");
  assert_equal("trace-contract-001", context.trace_id,
               "InfraContext should preserve AgentRequest trace_id semantics");
  assert_equal("task-contract-001", context.task_id,
               "InfraContext should consume WorkerTask task_id semantics only");
  assert_equal("task-parent-contract-001", context.parent_task_id,
               "InfraContext should consume WorkerTask parent_task_id semantics only");
  assert_equal("lease-from-task-001", context.lease_id,
               "InfraContext should consume WorkerTask lease_id without rewriting semantics");
}

void test_infra_context_never_emits_empty_identifier_strings() {
  using dasall::infra::InfraContext;
  using dasall::tests::support::assert_equal;

  const auto request = make_agent_request(
    std::string(), std::string(), std::nullopt, dasall::contracts::RequestChannel::Cli);

  const auto task = make_worker_task(std::string(), std::nullopt, std::string());
  const auto lease = make_worker_lease("lease-fallback-contract-001");

  const auto context = InfraContext::from_contracts(request, &task, &lease);

  assert_equal("unknown", context.request_id,
               "InfraContext should normalize empty request_id to unknown");
  assert_equal("unknown", context.session_id,
               "InfraContext should normalize empty session_id to unknown");
  assert_equal("unknown", context.trace_id,
               "InfraContext should normalize missing trace_id to unknown");
  assert_equal("unknown", context.task_id,
               "InfraContext should normalize empty task_id to unknown");
  assert_equal("unknown", context.parent_task_id,
               "InfraContext should normalize missing parent_task_id to unknown");
  assert_equal("lease-fallback-contract-001", context.lease_id,
               "InfraContext should use WorkerLease as fallback lease anchor");
}

}  // namespace

int main() {
  try {
    test_infra_context_consumes_only_existing_contract_identifiers();
    test_infra_context_never_emits_empty_identifier_strings();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
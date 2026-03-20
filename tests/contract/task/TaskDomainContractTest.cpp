#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "dasall/tests/support/TestAssertions.h"
#include "task/TaskDomainContracts.h"
#include "task/TaskDomainGuards.h"

namespace {

using dasall::contracts::SubTaskGraph;
using dasall::contracts::TaskDomainBoundaryDecision;
using dasall::contracts::TaskDomainObject;
using dasall::contracts::evaluate_task_domain_forbidden_field;
using dasall::contracts::is_runtime_controlled_task_domain_object;
using dasall::contracts::is_task_domain_object;
using dasall::contracts::kTaskDomainObjectCatalog;
using dasall::contracts::task_domain_object_name;
using dasall::contracts::task_domain_upstream_anchor;
using dasall::contracts::validate_subtask_graph_field_rules;
using dasall::contracts::validate_subtask_graph_required_fields;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

// Compile-time probes ensure SubTaskGraph does not accidentally expose
// top-level ownership fields.
template <typename T, typename = void>
struct has_session_id : std::false_type {};

template <typename T>
struct has_session_id<T, std::void_t<decltype(std::declval<T>().session_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_checkpoint_ref : std::false_type {};

template <typename T>
struct has_checkpoint_ref<
    T,
    std::void_t<decltype(std::declval<T>().checkpoint_ref)>> : std::true_type {
};

template <typename T, typename = void>
struct has_agent_result : std::false_type {};

template <typename T>
struct has_agent_result<T, std::void_t<decltype(std::declval<T>().agent_result)>>
    : std::true_type {};

// Builds a valid SubTaskGraph sample used by positive-path tests.
SubTaskGraph make_valid_subtask_graph() {
  SubTaskGraph graph;
  graph.graph_id = "subgraph-001";
  graph.root_task_id = "root-task-001";
  graph.task_ids = std::vector<std::string>{"task-a", "task-b"};
  graph.graph_revision = 1U;
  return graph;
}

void test_task_domain_catalog_contains_three_objects() {
  assert_equal(static_cast<int>(3),
               static_cast<int>(kTaskDomainObjectCatalog.size()),
               "task domain catalog should expose exactly three objects");

  assert_equal(
      "WorkerTask",
      std::string(task_domain_object_name(TaskDomainObject::WorkerTask)),
      "catalog should contain WorkerTask");
  assert_equal(
      "WorkerLease",
      std::string(task_domain_object_name(TaskDomainObject::WorkerLease)),
      "catalog should contain WorkerLease");
  assert_equal(
      "SubTaskGraph",
      std::string(task_domain_object_name(TaskDomainObject::SubTaskGraph)),
      "catalog should contain SubTaskGraph");
}

void test_task_domain_catalog_preserves_layering_anchors() {
  assert_equal(
      "AgentOrchestrator task graph",
      std::string(task_domain_upstream_anchor(TaskDomainObject::WorkerTask)),
      "WorkerTask should keep orchestrator task-graph anchor");
  assert_equal(
      "WorkerTask / top-level checkpoint subdomain snapshot",
      std::string(task_domain_upstream_anchor(TaskDomainObject::WorkerLease)),
      "WorkerLease should keep worker-task/checkpoint snapshot anchor");
  assert_equal(
      "MultiAgentCoordinator collaboration subgraph",
      std::string(task_domain_upstream_anchor(TaskDomainObject::SubTaskGraph)),
      "SubTaskGraph should keep collaboration-subgraph anchor");

  assert_true(
      is_runtime_controlled_task_domain_object(TaskDomainObject::WorkerTask),
      "WorkerTask must remain runtime controlled");
  assert_true(
      is_runtime_controlled_task_domain_object(TaskDomainObject::WorkerLease),
      "WorkerLease must remain runtime controlled");
  assert_true(
      is_runtime_controlled_task_domain_object(TaskDomainObject::SubTaskGraph),
      "SubTaskGraph must remain runtime controlled");
}

void test_object_lookup_accepts_only_task_domain_objects() {
  assert_true(is_task_domain_object("WorkerTask"),
              "WorkerTask should be recognized by task catalog");
  assert_true(is_task_domain_object("WorkerLease"),
              "WorkerLease should be recognized by task catalog");
  assert_true(is_task_domain_object("SubTaskGraph"),
              "SubTaskGraph should be recognized by task catalog");

  assert_true(!is_task_domain_object("AgentRequest"),
              "AgentRequest is an upstream object and must not be duplicated");
  assert_true(!is_task_domain_object("AgentResult"),
              "AgentResult is an upstream object and must not be duplicated");
}

void test_valid_subtask_graph_passes_required_and_field_guards() {
  const auto graph = make_valid_subtask_graph();

  const auto required_result = validate_subtask_graph_required_fields(graph);
  assert_true(required_result.ok,
              "valid subtask graph should pass required fields guard");

  const auto field_result = validate_subtask_graph_field_rules(graph);
  assert_true(field_result.ok,
              "valid subtask graph should pass field rules guard");
}

void test_missing_graph_id_fails_required_guard() {
  auto graph = make_valid_subtask_graph();
  graph.graph_id = std::nullopt;

  const auto result = validate_subtask_graph_required_fields(graph);
  assert_true(!result.ok, "missing graph_id must fail required guard");
  assert_equal("graph_id is required and must be non-empty",
               std::string(result.reason),
               "missing graph_id should return canonical reason");
}

void test_whitespace_task_id_fails_field_rules_guard() {
  auto graph = make_valid_subtask_graph();
  graph.task_ids = std::vector<std::string>{"task-a", "   "};

  const auto result = validate_subtask_graph_field_rules(graph);
  assert_true(!result.ok, "whitespace task_id must fail field-rules guard");
  assert_equal("task_ids must not contain empty or whitespace-only items",
               std::string(result.reason),
               "whitespace task_id should return canonical reason");
}

void test_task_domain_forbidden_fields_are_rejected_with_stable_decisions() {
  const auto global_state_result =
      evaluate_task_domain_forbidden_field("global_fsm_state");
  assert_true(!global_state_result.ok,
              "global_fsm_state must be rejected for task domain");
  assert_equal(
      static_cast<int>(TaskDomainBoundaryDecision::RejectGlobalStateField),
      static_cast<int>(global_state_result.decision),
      "global state rejection should keep normalized decision");

  const auto checkpoint_result =
      evaluate_task_domain_forbidden_field("checkpoint_ref");
  assert_true(!checkpoint_result.ok,
              "checkpoint_ref must be rejected for task domain");
  assert_equal(
      static_cast<int>(TaskDomainBoundaryDecision::RejectRecoveryEntryField),
      static_cast<int>(checkpoint_result.decision),
      "checkpoint rejection should keep normalized decision");

  const auto result_alias =
      evaluate_task_domain_forbidden_field("agent_result");
  assert_true(!result_alias.ok,
              "agent_result must be rejected for task domain");
  assert_equal(
      static_cast<int>(TaskDomainBoundaryDecision::RejectFinalResultField),
      static_cast<int>(result_alias.decision),
      "final-result rejection should keep normalized decision");
}

void test_compile_time_shape_does_not_reuse_top_level_fields() {
  static_assert(!has_session_id<SubTaskGraph>::value,
                "SubTaskGraph must not expose session_id");
  static_assert(!has_checkpoint_ref<SubTaskGraph>::value,
                "SubTaskGraph must not expose checkpoint_ref");
  static_assert(!has_agent_result<SubTaskGraph>::value,
                "SubTaskGraph must not expose agent_result");
}

}  // namespace

int main() {
  try {
    test_task_domain_catalog_contains_three_objects();
    test_task_domain_catalog_preserves_layering_anchors();
    test_object_lookup_accepts_only_task_domain_objects();
    test_valid_subtask_graph_passes_required_and_field_guards();
    test_missing_graph_id_fails_required_guard();
    test_whitespace_task_id_fails_field_rules_guard();
    test_task_domain_forbidden_fields_are_rejected_with_stable_decisions();
    test_compile_time_shape_does_not_reuse_top_level_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

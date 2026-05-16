#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "validation/StageOutputValidator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::plan::PlanEdge;
using dasall::cognition::plan::PlanGraph;
using dasall::cognition::plan::PlanNode;
using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::ValidationIssueCode;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] PlanGraph make_valid_plan_graph() {
  return PlanGraph{
      .plan_id = "plan-021-valid",
      .revision = 1U,
      .nodes = {
          PlanNode{.node_id = "n1", .objective = "collect facts", .success_signal = "facts", .action_kind_hint = "analyze", .depends_on = {}, .evidence_refs = {}},
          PlanNode{.node_id = "n2", .objective = "execute action", .success_signal = "action", .action_kind_hint = "tool", .depends_on = {"n1"}, .evidence_refs = {}},
      },
      .edges = {
          PlanEdge{.from_node_id = "n1", .to_node_id = "n2", .condition = "facts_ready", .evidence_refs = {}},
      },
      .open_questions = {},
      .plan_rationale = "two-step plan",
      .estimated_complexity = 2U,
  };
}

void test_validate_plan_graph_invariants_accepts_valid_dag() {
  StageOutputValidator validator;
  const auto result = validator.validate_plan_graph_invariants(make_valid_plan_graph(), 4U, 3U);

  assert_true(result.ok, "valid DAG plan graphs should pass invariant validation");
  assert_true(result.issue_set.empty(),
              "valid DAG plan graphs should not emit invariant issues");
}

void test_validate_plan_graph_invariants_rejects_depth_cap_violations() {
  StageOutputValidator validator;
  auto plan_graph = make_valid_plan_graph();
  plan_graph.nodes.push_back(
      PlanNode{.node_id = "n3", .objective = "respond", .success_signal = "response", .action_kind_hint = "respond", .depends_on = {"n2"}, .evidence_refs = {}});
  plan_graph.edges.push_back(
      PlanEdge{.from_node_id = "n2", .to_node_id = "n3", .condition = "action_done", .evidence_refs = {}});

  const auto result = validator.validate_plan_graph_invariants(plan_graph, 4U, 2U);

  assert_true(!result.ok, "plans exceeding the configured depth cap must fail invariants");
  assert_equal(1, static_cast<int>(result.issue_set.issues.size()),
               "depth cap violations should surface a single invariant issue");
  assert_true(result.issue_set.issues.front().code == ValidationIssueCode::PlanGraphInvariant,
              "plan graph failures must use the plan-graph invariant code");
}

void test_validate_plan_graph_invariants_rejects_unknown_depends_on_references() {
  StageOutputValidator validator;
  auto plan_graph = make_valid_plan_graph();
  plan_graph.nodes[1].depends_on = {"missing-node"};
  plan_graph.edges.clear();

  const auto result = validator.validate_plan_graph_invariants(plan_graph, 4U, 3U);

  assert_true(!result.ok, "depends_on references to unknown nodes must fail invariants");
  assert_true(result.issue_set.issues.front().code == ValidationIssueCode::PlanGraphInvariant,
              "depends_on failures must use the plan-graph invariant code");
}

void test_validate_plan_graph_invariants_rejects_duplicate_and_self_dependencies() {
  StageOutputValidator validator;
  auto plan_graph = make_valid_plan_graph();
  plan_graph.nodes[1].depends_on = {"n2", "n1", "n1"};
  plan_graph.edges.clear();

  const auto result = validator.validate_plan_graph_invariants(plan_graph, 4U, 3U);

  assert_true(!result.ok, "duplicate and self dependencies must fail invariants");
  assert_true(result.issue_set.issues.size() >= 2U,
              "duplicate and self dependency violations should both be surfaced");
}

void test_validate_plan_graph_invariants_rejects_depends_on_cycles_without_edges() {
  StageOutputValidator validator;
  auto plan_graph = make_valid_plan_graph();
  plan_graph.nodes[0].depends_on = {"n2"};
  plan_graph.nodes[1].depends_on = {"n1"};
  plan_graph.edges.clear();

  const auto result = validator.validate_plan_graph_invariants(plan_graph, 4U, 3U);

  assert_true(!result.ok, "depends_on cycles must fail invariants even when edges are absent");
}

void test_validate_plan_graph_invariants_rejects_depends_on_depth_cap_violations() {
  StageOutputValidator validator;
  auto plan_graph = make_valid_plan_graph();
  plan_graph.nodes.push_back(
      PlanNode{.node_id = "n3", .objective = "respond", .success_signal = "response", .action_kind_hint = "respond", .depends_on = {"n2"}, .evidence_refs = {}});
  plan_graph.edges.clear();

  const auto result = validator.validate_plan_graph_invariants(plan_graph, 4U, 2U);

  assert_true(!result.ok, "depends_on chains must participate in the configured depth cap");
}

}  // namespace

int main() {
  try {
    test_validate_plan_graph_invariants_accepts_valid_dag();
    test_validate_plan_graph_invariants_rejects_depth_cap_violations();
    test_validate_plan_graph_invariants_rejects_unknown_depends_on_references();
    test_validate_plan_graph_invariants_rejects_duplicate_and_self_dependencies();
    test_validate_plan_graph_invariants_rejects_depends_on_cycles_without_edges();
    test_validate_plan_graph_invariants_rejects_depends_on_depth_cap_violations();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
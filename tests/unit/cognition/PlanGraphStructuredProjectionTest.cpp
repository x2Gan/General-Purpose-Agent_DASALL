#include <exception>
#include <iostream>
#include <string>

#include "projection/PlanGraphStructuredProjector.h"
#include "support/TestAssertions.h"
#include "validation/StageOutputValidator.h"
#include "validation/StructuredPayloadView.h"

namespace {

using dasall::cognition::projection::PlanGraphStructuredProjector;
using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::StructuredPayloadView;
using dasall::cognition::validation::ValidationIssueCode;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] StructuredPayloadView parse_payload_or_throw(const std::string& payload) {
  const auto payload_view = StructuredPayloadView::parse_structured_payload(payload);
  assert_true(payload_view.has_value(), "test payload should remain well-formed JSON");
  return *payload_view;
}

[[nodiscard]] std::string make_valid_planning_payload() {
  return R"({
    "schema_version":"cognition.plan.v1",
    "plan_id":"plan-structured-001",
    "revision":1,
    "nodes":[
      {
        "node_id":"n1",
        "objective":"collect quarterly sales data",
        "success_signal":"sales_data_collected",
        "action_kind_hint":"tool_action",
        "depends_on":[],
        "evidence_refs":["dataset:quarterly-sales"]
      },
      {
        "node_id":"n2",
        "objective":"validate Berlin-specific findings",
        "success_signal":"berlin_findings_validated",
        "action_kind_hint":"validation",
        "depends_on":["n1"],
        "evidence_refs":["analysis:berlin"]
      }
    ],
    "edges":[
      {
        "from_node_id":"n1",
        "to_node_id":"n2",
        "condition":"sales_data_collected",
        "evidence_refs":["edge:n1-n2"]
      }
    ],
    "open_questions":[
      {
        "question_id":"q1",
        "question":"Should the report include quarter-over-quarter deltas?",
        "reason":"output format not fixed",
        "blocks_plan":false,
        "evidence_refs":["question:q1"]
      }
    ],
    "plan_rationale":"collect evidence before validating the final answer",
    "estimated_complexity":2
  })";
}

void test_project_plan_graph_accepts_valid_structured_payload() {
  PlanGraphStructuredProjector projector;
  StageOutputValidator validator;

  const auto result = projector.project_plan_graph(parse_payload_or_throw(make_valid_planning_payload()));

  assert_true(result.ok, "valid planning payload should project to a PlanGraph");
  assert_true(result.plan_graph.has_value(), "successful projection should return a PlanGraph");
  assert_equal(std::string("plan-structured-001"), result.plan_graph->plan_id,
               "projector should preserve plan_id");
  assert_equal(2, static_cast<int>(result.plan_graph->nodes.size()),
               "projector should preserve plan nodes");
  assert_equal(1, static_cast<int>(result.plan_graph->open_questions.size()),
               "projector should preserve open questions");

  const auto invariant_result = validator.validate_plan_graph_invariants(*result.plan_graph, 4U, 3U);
  assert_true(invariant_result.ok,
              "projected plan graphs should satisfy existing invariant validation on the happy path");
}

void test_project_plan_graph_rejects_missing_success_signal() {
  PlanGraphStructuredProjector projector;
  const auto payload = R"({
    "schema_version":"cognition.plan.v1",
    "plan_id":"plan-structured-002",
    "revision":1,
    "nodes":[
      {
        "node_id":"n1",
        "objective":"collect quarterly sales data",
        "success_signal":"",
        "action_kind_hint":"tool_action",
        "depends_on":[],
        "evidence_refs":[]
      }
    ],
    "edges":[],
    "open_questions":[],
    "plan_rationale":"missing signal should fail",
    "estimated_complexity":1
  })";

  const auto result = projector.project_plan_graph(parse_payload_or_throw(payload));

  assert_true(!result.ok, "missing success_signal must fail projection");
  assert_true(!result.plan_graph.has_value(), "failed projection must not return a partial PlanGraph");
  assert_true(result.error_info.has_value(), "failed projection must return an ErrorInfo payload");
}

void test_project_plan_graph_rejects_duplicate_node_ids() {
  PlanGraphStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.plan.v1",
    "plan_id":"plan-structured-003",
    "revision":1,
    "nodes":[
      {
        "node_id":"n1",
        "objective":"collect quarterly sales data",
        "success_signal":"sales_data_collected",
        "action_kind_hint":"tool_action",
        "depends_on":[],
        "evidence_refs":[]
      },
      {
        "node_id":"n1",
        "objective":"validate Berlin-specific findings",
        "success_signal":"berlin_findings_validated",
        "action_kind_hint":"validation",
        "depends_on":["n1"],
        "evidence_refs":[]
      }
    ],
    "edges":[{"from_node_id":"n1","to_node_id":"n1","condition":"dup","evidence_refs":[]}],
    "open_questions":[],
    "plan_rationale":"duplicate node ids should fail invariants",
    "estimated_complexity":2
  })";

  const auto result = projector.project_plan_graph(parse_payload_or_throw(payload));
  assert_true(result.ok && result.plan_graph.has_value(),
              "duplicate node ids are a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_plan_graph_invariants(*result.plan_graph, 4U, 3U);
  assert_true(!invariant_result.ok, "duplicate node ids must fail invariant validation");
  assert_true(invariant_result.issue_set.issues.front().code == ValidationIssueCode::PlanGraphInvariant,
              "duplicate node ids should surface a plan graph invariant issue");
}

void test_project_plan_graph_rejects_unknown_edge_references() {
  PlanGraphStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.plan.v1",
    "plan_id":"plan-structured-004",
    "revision":1,
    "nodes":[
      {
        "node_id":"n1",
        "objective":"collect quarterly sales data",
        "success_signal":"sales_data_collected",
        "action_kind_hint":"tool_action",
        "depends_on":[],
        "evidence_refs":[]
      }
    ],
    "edges":[{"from_node_id":"n1","to_node_id":"n2","condition":"missing-target","evidence_refs":[]}],
    "open_questions":[],
    "plan_rationale":"unknown edge target should fail invariants",
    "estimated_complexity":1
  })";

  const auto result = projector.project_plan_graph(parse_payload_or_throw(payload));
  assert_true(result.ok && result.plan_graph.has_value(),
              "unknown edge references are a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_plan_graph_invariants(*result.plan_graph, 4U, 3U);
  assert_true(!invariant_result.ok, "unknown edge references must fail invariant validation");
}

void test_project_plan_graph_rejects_cycles() {
  PlanGraphStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.plan.v1",
    "plan_id":"plan-structured-005",
    "revision":1,
    "nodes":[
      {
        "node_id":"n1",
        "objective":"collect quarterly sales data",
        "success_signal":"sales_data_collected",
        "action_kind_hint":"tool_action",
        "depends_on":["n2"],
        "evidence_refs":[]
      },
      {
        "node_id":"n2",
        "objective":"validate Berlin-specific findings",
        "success_signal":"berlin_findings_validated",
        "action_kind_hint":"validation",
        "depends_on":["n1"],
        "evidence_refs":[]
      }
    ],
    "edges":[
      {"from_node_id":"n1","to_node_id":"n2","condition":"forward","evidence_refs":[]},
      {"from_node_id":"n2","to_node_id":"n1","condition":"backward","evidence_refs":[]}
    ],
    "open_questions":[],
    "plan_rationale":"cycles should fail invariants",
    "estimated_complexity":2
  })";

  const auto result = projector.project_plan_graph(parse_payload_or_throw(payload));
  assert_true(result.ok && result.plan_graph.has_value(),
              "cycles are a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_plan_graph_invariants(*result.plan_graph, 4U, 3U);
  assert_true(!invariant_result.ok, "cyclic plans must fail invariant validation");
}

void test_project_plan_graph_rejects_node_cap_violations() {
  PlanGraphStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.plan.v1",
    "plan_id":"plan-structured-006",
    "revision":1,
    "nodes":[
      {"node_id":"n1","objective":"collect data","success_signal":"data","action_kind_hint":"tool_action","depends_on":[],"evidence_refs":[]},
      {"node_id":"n2","objective":"validate data","success_signal":"validated","action_kind_hint":"validation","depends_on":["n1"],"evidence_refs":[]},
      {"node_id":"n3","objective":"prepare response","success_signal":"response_ready","action_kind_hint":"direct_response","depends_on":["n2"],"evidence_refs":[]}
    ],
    "edges":[
      {"from_node_id":"n1","to_node_id":"n2","condition":"data_ready","evidence_refs":[]},
      {"from_node_id":"n2","to_node_id":"n3","condition":"validated","evidence_refs":[]}
    ],
    "open_questions":[],
    "plan_rationale":"node cap violations should fail invariants",
    "estimated_complexity":3
  })";

  const auto result = projector.project_plan_graph(parse_payload_or_throw(payload));
  assert_true(result.ok && result.plan_graph.has_value(),
              "node cap violations are a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_plan_graph_invariants(*result.plan_graph, 2U, 4U);
  assert_true(!invariant_result.ok, "plans exceeding max_plan_nodes must fail invariant validation");
}

}  // namespace

int main() {
  try {
    test_project_plan_graph_accepts_valid_structured_payload();
    test_project_plan_graph_rejects_missing_success_signal();
    test_project_plan_graph_rejects_duplicate_node_ids();
    test_project_plan_graph_rejects_unknown_edge_references();
    test_project_plan_graph_rejects_cycles();
    test_project_plan_graph_rejects_node_cap_violations();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
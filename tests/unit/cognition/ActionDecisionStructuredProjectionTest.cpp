#include <exception>
#include <iostream>
#include <string>

#include "projection/ActionDecisionStructuredProjector.h"
#include "support/TestAssertions.h"
#include "validation/StageOutputValidator.h"
#include "validation/StructuredPayloadView.h"

namespace {

using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::projection::ActionDecisionStructuredProjector;
using dasall::cognition::validation::StageOutputValidator;
using dasall::cognition::validation::StructuredPayloadView;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] StructuredPayloadView parse_payload_or_throw(const std::string& payload) {
  const auto payload_view = StructuredPayloadView::parse_structured_payload(payload);
  assert_true(payload_view.has_value(), "test payload should remain well-formed JSON");
  return *payload_view;
}

[[nodiscard]] std::string make_valid_execution_payload() {
  return R"({
    "schema_version":"cognition.reasoning.v1",
    "decision_kind":"ExecuteAction",
    "confidence":0.84,
    "rationale":"execute the highest-confidence actionable step",
    "selected_node_id":"node-execute-1",
    "tool_intent_hint":{
      "tool_name":"agent.dataset",
      "intent_summary":"gather the quarterly sales dataset for Berlin",
      "argument_hints":["Berlin quarterly sales","return evidence only"],
      "evidence_refs":["belief:evidence:001"]
    },
    "clarification_needed":false,
    "clarification_question":null,
    "response_outline":{
      "summary":"execute: gather the quarterly sales dataset for Berlin",
      "key_points":["collect evidence first","preserve source refs"]
    },
    "candidate_scores":[
      {
        "candidate_name":"execute_action",
        "score":0.84,
        "rationale":"active node and tool are available"
      },
      {
        "candidate_name":"direct_response",
        "score":0.24,
        "rationale":"not enough evidence to answer directly"
      }
    ]
  })";
}

void test_project_action_decision_accepts_valid_structured_payload() {
  ActionDecisionStructuredProjector projector;
  StageOutputValidator validator;

  const auto result = projector.project_action_decision(
      parse_payload_or_throw(make_valid_execution_payload()));

  assert_true(result.ok, "valid execution payload should project to an ActionDecision");
  assert_true(result.action_decision.has_value(),
              "successful projection should return an ActionDecision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::ExecuteAction,
              "projector should preserve decision_kind");
  assert_true(result.action_decision->selected_node_id.has_value(),
              "execute action should preserve selected_node_id");
  assert_equal(std::string("node-execute-1"), *result.action_decision->selected_node_id,
               "projector should preserve selected_node_id");
  assert_true(result.action_decision->tool_intent_hint.has_value(),
              "execute action should preserve tool intent hints");
  assert_equal(std::string("agent.dataset"), result.action_decision->tool_intent_hint->tool_name,
               "projector should preserve tool_name");
  assert_equal(2, static_cast<int>(result.action_decision->candidate_scores.size()),
               "projector should preserve structured candidate scores");

  const auto invariant_result = validator.validate_action_decision_invariants(
      *result.action_decision);
  assert_true(invariant_result.ok,
              "projected action decisions should satisfy existing invariant validation on the happy path");
}

void test_project_action_decision_rejects_invalid_enum_literal() {
  ActionDecisionStructuredProjector projector;
  const auto payload = R"({
    "schema_version":"cognition.reasoning.v1",
    "decision_kind":"LaunchAction",
    "confidence":0.84,
    "rationale":"invalid decision kind should fail projection",
    "selected_node_id":"node-execute-1",
    "tool_intent_hint":null,
    "clarification_needed":false,
    "clarification_question":null,
    "response_outline":null,
    "candidate_scores":[
      {
        "candidate_name":"execute_action",
        "score":0.84,
        "rationale":"invalid enum"
      }
    ]
  })";

  const auto result = projector.project_action_decision(parse_payload_or_throw(payload));

  assert_true(!result.ok, "invalid decision_kind must fail projection");
  assert_true(!result.action_decision.has_value(),
              "failed projection must not return a partial ActionDecision");
  assert_true(result.error_info.has_value(), "failed projection must return an ErrorInfo payload");
}

void test_project_action_decision_rejects_missing_selected_node() {
  ActionDecisionStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.reasoning.v1",
    "decision_kind":"ExecuteAction",
    "confidence":0.84,
    "rationale":"missing selected node should fail invariants",
    "selected_node_id":null,
    "tool_intent_hint":{
      "tool_name":"agent.dataset",
      "intent_summary":"gather the quarterly sales dataset for Berlin",
      "argument_hints":[],
      "evidence_refs":[]
    },
    "clarification_needed":false,
    "clarification_question":null,
    "response_outline":null,
    "candidate_scores":[
      {
        "candidate_name":"execute_action",
        "score":0.84,
        "rationale":"selected node missing"
      }
    ]
  })";

  const auto result = projector.project_action_decision(parse_payload_or_throw(payload));
  assert_true(result.ok && result.action_decision.has_value(),
              "missing selected_node_id is a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_action_decision_invariants(
      *result.action_decision);
  assert_true(!invariant_result.ok, "execute_action without selected_node_id must fail invariants");
}

void test_project_action_decision_rejects_tool_intent_on_response() {
  ActionDecisionStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.reasoning.v1",
    "decision_kind":"DirectResponse",
    "confidence":0.73,
    "rationale":"response path should not carry tool intent",
    "selected_node_id":null,
    "tool_intent_hint":{
      "tool_name":"agent.dataset",
      "intent_summary":"this should not be present on response",
      "argument_hints":[],
      "evidence_refs":[]
    },
    "clarification_needed":false,
    "clarification_question":null,
    "response_outline":{
      "summary":"respond with the current evidence snapshot",
      "key_points":["do not execute tools"]
    },
    "candidate_scores":[
      {
        "candidate_name":"direct_response",
        "score":0.73,
        "rationale":"response wins"
      }
    ]
  })";

  const auto result = projector.project_action_decision(parse_payload_or_throw(payload));
  assert_true(result.ok && result.action_decision.has_value(),
              "tool intent on response is a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_action_decision_invariants(
      *result.action_decision);
  assert_true(!invariant_result.ok, "response decisions must fail when tool intent is present");
}

void test_project_action_decision_rejects_clarification_conflict() {
  ActionDecisionStructuredProjector projector;
  StageOutputValidator validator;
  const auto payload = R"({
    "schema_version":"cognition.reasoning.v1",
    "decision_kind":"ExecuteAction",
    "confidence":0.84,
    "rationale":"execute action should not request clarification simultaneously",
    "selected_node_id":"node-execute-1",
    "tool_intent_hint":{
      "tool_name":"agent.dataset",
      "intent_summary":"gather the quarterly sales dataset for Berlin",
      "argument_hints":[],
      "evidence_refs":[]
    },
    "clarification_needed":true,
    "clarification_question":"Which quarter should I prioritize?",
    "response_outline":null,
    "candidate_scores":[
      {
        "candidate_name":"execute_action",
        "score":0.84,
        "rationale":"clarification conflict"
      }
    ]
  })";

  const auto result = projector.project_action_decision(parse_payload_or_throw(payload));
  assert_true(result.ok && result.action_decision.has_value(),
              "clarification conflicts are a typed projection success but invariant failure");

  const auto invariant_result = validator.validate_action_decision_invariants(
      *result.action_decision);
  assert_true(!invariant_result.ok,
              "execute_action decisions must fail when clarification is simultaneously requested");
}

}  // namespace

int main() {
  try {
    test_project_action_decision_accepts_valid_structured_payload();
    test_project_action_decision_rejects_invalid_enum_literal();
    test_project_action_decision_rejects_missing_selected_node();
    test_project_action_decision_rejects_tool_intent_on_response();
    test_project_action_decision_rejects_clarification_conflict();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
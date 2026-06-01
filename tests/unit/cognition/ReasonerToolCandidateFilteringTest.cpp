#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "reasoning/Reasoner.h"
#include "support/TestAssertions.h"
#include "tool/ToolDescriptor.h"

namespace {

using dasall::cognition::CognitionConfig;
using dasall::cognition::ReasoningRequest;
using dasall::cognition::decision::ActionDecision;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::perception::EntityCandidate;
using dasall::cognition::reasoning::Reasoner;
using dasall::contracts::ToolCategory;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolDescriptor make_tool_descriptor(
    std::string tool_name,
    std::string display_name,
    std::vector<std::string> tags,
    ToolCategory category = ToolCategory::Information) {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::move(tool_name),
      .display_name = std::move(display_name),
      .category = category,
      .capability_tier = std::nullopt,
      .is_read_only = true,
      .supports_compensation = std::nullopt,
      .default_timeout_ms = std::nullopt,
      .input_schema_ref = std::nullopt,
      .output_schema_ref = std::nullopt,
      .required_scopes = std::nullopt,
      .tags = std::move(tags),
      .version = std::string("v1"),
  };
}

[[nodiscard]] bool has_diagnostic(const ActionDecision& decision,
                                  std::string_view diagnostic) {
  for (const auto& entry : decision.diagnostics) {
    if (entry == diagnostic) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] ReasoningRequest make_prefilter_request() {
  ReasoningRequest request;
  request.caller_domain = "runtime.agent_orchestrator";
  request.request_id = "req-015-tool-prefilter";
  request.trace_id = "trace-015-tool-prefilter";
  request.profile_id = "desktop_full";

  request.goal_contract.goal_id = std::string("goal-015-tool-prefilter");
  request.goal_contract.request_id = std::string("req-015-tool-prefilter");
  request.goal_contract.goal_description =
      std::string("search Berlin quarterly sales data and summarize the evidence");
  request.goal_contract.success_criteria =
      std::string("return an evidence-backed summary for Berlin quarterly sales");
  request.goal_contract.status = dasall::contracts::GoalStatus::Active;
  request.goal_contract.created_at = 1712345600000;

  request.context_packet.request_id = std::string("req-015-tool-prefilter");
  request.context_packet.user_turn =
      std::string("Use the right tool to search Berlin sales records and summarize them");
  request.context_packet.current_goal_summary =
      std::string("search sales evidence and summarize the result");
  request.context_packet.active_tools =
      std::vector<std::string>{std::string("agent.dataset"),
                               std::string("agent.search"),
                               std::string("agent.report"),
                               std::string("agent.calendar"),
                               std::string("agent.terminal")};

  request.belief_state.request_id = std::string("req-015-tool-prefilter");
  request.belief_state.confirmed_facts =
      std::vector<std::string>{std::string("Berlin is the requested city")};
  request.belief_state.hypotheses =
      std::vector<std::string>{std::string("quarterly sales data is accessible through a tool")};
  request.belief_state.assumptions =
      std::vector<std::string>{std::string("dataset search is the safest first step")};
  request.belief_state.evidence_refs =
      std::vector<std::string>{std::string("belief:evidence:015")};
  request.belief_state.confidence = 0.88F;

  request.perception_result.intent_summary =
      std::string("search the Berlin sales dataset and summarize evidence");
  request.perception_result.task_type = std::string("action_decision");
  request.perception_result.entities = {
      EntityCandidate{.name = "tool",
                      .value = "agent.dataset",
                      .confidence = 0.96F,
                      .evidence_refs = {std::string("tool:agent.dataset")}},
  };
  request.perception_result.confidence = 0.86F;

  request.active_plan.plan_id = std::string("plan-015-tool-prefilter");
  request.active_plan.revision = 1U;
  request.active_plan.nodes = {
      {.node_id = "plan-015-tool-prefilter-node-1",
       .objective = "Use a tool to search Berlin quarterly sales data and summarize the evidence",
       .success_signal = "Berlin quarterly sales evidence is available for summary",
       .action_kind_hint = "tool_execution",
       .depends_on = {},
       .evidence_refs = {std::string("belief:evidence:015")}},
  };
  request.active_plan.plan_rationale =
      std::string("planner selected a single evidence-gathering tool step");
  request.active_plan.estimated_complexity = 1U;

  return request;
}

void test_reasoner_prefilters_large_tool_catalog_to_top_k() {
  Reasoner reasoner(CognitionConfig{});
  auto request = make_prefilter_request();
  request.available_tool_descriptors = {
      make_tool_descriptor("agent.dataset", "Dataset Evidence Search",
                           {"dataset", "sales", "berlin", "evidence"}),
      make_tool_descriptor("agent.search", "Evidence Search",
                           {"search", "sales", "evidence"}),
      make_tool_descriptor("agent.report", "Summary Report Builder",
                           {"summary", "report", "sales"}),
      make_tool_descriptor("agent.calendar", "Calendar Coordinator",
                           {"meeting", "schedule", "calendar"}, ToolCategory::Action),
      make_tool_descriptor("agent.terminal", "Terminal Shell",
                           {"shell", "command", "diagnostic"}, ToolCategory::Diagnostic),
  };

  const auto decision = reasoner.decide(request);

  assert_true(decision.decision_kind == ActionDecisionKind::ExecuteAction,
              "actionable request should stay on the execute-action path");
  assert_true(decision.tool_intent_hint.has_value(),
              "execute-action decisions must include a tool intent hint");
  assert_equal(std::string("agent.dataset"), decision.tool_intent_hint->tool_name,
               "prefilter should keep the exact perception-matched tool at the head of the top-K set");
  assert_true(has_diagnostic(decision, "tool_candidate_prefilter:applied"),
              "large tool catalogs should mark that prefiltering was applied");
  assert_true(has_diagnostic(decision, "tool_candidate_prefilter_count:3"),
              "large tool catalogs should be reduced to the top-K candidate count");
}

void test_reasoner_falls_back_to_legacy_tool_selection_when_descriptors_are_absent() {
  Reasoner reasoner(CognitionConfig{});
  auto request = make_prefilter_request();
  request.context_packet.active_tools =
      std::vector<std::string>{std::string("agent.dataset")};
  request.available_tool_descriptors.clear();

  const auto decision = reasoner.decide(request);

  assert_true(decision.decision_kind == ActionDecisionKind::ExecuteAction,
              "descriptor absence should not force a non-action decision");
  assert_true(decision.tool_intent_hint.has_value(),
              "legacy tool selection should still emit a tool hint");
  assert_equal(std::string("agent.dataset"), decision.tool_intent_hint->tool_name,
               "without descriptors the reasoner should fall back to the existing tool selection path");
  assert_true(!has_diagnostic(decision, "tool_candidate_prefilter:applied"),
              "prefilter diagnostics should stay absent when descriptors are unavailable");
  assert_true(!has_diagnostic(decision, "tool_candidate_prefilter_count:3"),
              "candidate-count diagnostics should stay absent when no prefilter ran");
}

}  // namespace

int main() {
  try {
    test_reasoner_prefilters_large_tool_catalog_to_top_k();
    test_reasoner_falls_back_to_legacy_tool_selection_when_descriptors_are_absent();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
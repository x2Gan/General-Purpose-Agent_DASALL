#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "MockCognitionTelemetrySink.h"
#include "decision/ActionDecision.h"
#include "observability/CognitionTelemetry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::decision::ActionDecision;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::decision::CandidateDecisionScore;
using dasall::cognition::observability::CognitionTelemetry;
using dasall::cognition::observability::StageTelemetryContext;
using dasall::cognition::observability::TelemetryField;
using dasall::tests::mocks::MockCognitionTelemetrySink;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool has_field(const std::vector<TelemetryField>& fields,
                             const std::string& key,
                             const std::string& value) {
  for (const auto& field : fields) {
    if (field.key == key && field.value == value) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] StageTelemetryContext make_context() {
  return StageTelemetryContext{
      .request_id = "req-cog-022",
      .goal_id = "goal-cog-022",
      .profile_id = "desktop_full",
      .stage = "execution",
      .trace_id = "trace-cog-022",
      .model_hint_tier = "standard",
      .fallback_used = false,
      .result_code = 0,
  };
}

[[nodiscard]] ActionDecision make_action_decision() {
  ActionDecision action_decision;
  action_decision.decision_kind = ActionDecisionKind::ExecuteAction;
  action_decision.selected_node_id = std::string{"plan-node-022"};
  action_decision.confidence = 0.81F;
  action_decision.candidate_scores = {
      CandidateDecisionScore{
          .candidate_name = "execute_action",
          .score = 0.81F,
          .rationale = std::string{"preferred candidate"},
      },
      CandidateDecisionScore{
          .candidate_name = "direct_response",
          .score = 0.22F,
          .rationale = std::string{"fallback candidate"},
      },
  };
  return action_decision;
}

void test_emit_stage_started_and_completed_propagates_required_fields() {
  auto sink = std::make_shared<MockCognitionTelemetrySink>();
  CognitionTelemetry telemetry(dasall::cognition::CognitionConfig{}, sink);
  const auto context = make_context();
  const auto action_decision = make_action_decision();

  const auto started = telemetry.emit_stage_started(context);
  const auto completed = telemetry.emit_stage_completed(
      context,
      CognitionTelemetry::make_decision_record(action_decision));

  assert_true(started.emitted, "stage started should emit telemetry across available sinks");
  assert_true(completed.emitted,
              "stage completed should emit telemetry across available sinks");
  assert_equal(2, static_cast<int>(sink->log_events.size()),
               "two log events should be recorded for started + completed");
  assert_equal(2, static_cast<int>(sink->metrics.size()),
               "two metrics should be recorded for started + completed");
  assert_equal(2, static_cast<int>(sink->trace_events.size()),
               "two trace events should be recorded when stage spans are enabled");
  assert_equal(2, static_cast<int>(sink->audit_events.size()),
               "two audit events should be recorded for started + completed");

  const auto& completed_event = sink->log_events.back();
  assert_true(has_field(completed_event.fields, "request_id", "req-cog-022"),
              "completed event should carry request_id");
  assert_true(has_field(completed_event.fields, "trace_id", "trace-cog-022"),
              "completed event should carry trace_id");
  assert_true(has_field(completed_event.fields, "stage", "execution"),
              "completed event should carry stage");
  assert_true(has_field(completed_event.fields, "model_hint_tier", "standard"),
              "completed event should carry model_hint_tier");
  assert_true(has_field(completed_event.fields, "decision_kind", "ExecuteAction"),
              "completed event should carry decision kind");
  assert_true(has_field(completed_event.fields, "selected_node_id", "plan-node-022"),
              "completed event should carry selected node id");
}

}  // namespace

int main() {
  try {
    test_emit_stage_started_and_completed_propagates_required_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
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
using dasall::cognition::observability::make_live_telemetry_sink;
using dasall::cognition::observability::StageTelemetryContext;
using dasall::cognition::observability::StructuredProjectionTelemetry;
using dasall::cognition::observability::TelemetryField;
using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
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
        .structured_projection = StructuredProjectionTelemetry{
          .enabled = true,
          .required = true,
          .schema_version = std::string{"cognition.reasoning.v1"},
          .source = std::string{"llm_bridge"},
          .failure_code = std::string{"projection"},
          .projected_node_count = 3U,
          .projected_candidate_count = 2U,
        },
  };
}

    [[nodiscard]] ErrorInfo make_error_info() {
      return ErrorInfo{
        .failure_type = dasall::contracts::classify_result_code(ResultCode::ValidationFieldMissing),
        .retryable = false,
        .safe_to_replan = false,
        .details = ErrorDetails{
          .code = static_cast<int>(ResultCode::ValidationFieldMissing),
          .message = "projection failed",
          .stage = "execution",
        },
        .source_ref = ErrorSourceRefMinimal{
          .ref_type = "cognition.projector",
          .ref_id = "action_decision",
        },
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
  assert_true(has_field(completed_event.fields, "structured_projection_enabled", "true"),
              "completed event should carry structured projection enabled flag");
  assert_true(has_field(completed_event.fields, "structured_projection_required", "true"),
              "completed event should carry structured projection required flag");
  assert_true(has_field(completed_event.fields,
                        "structured_schema_version",
                        "cognition.reasoning.v1"),
              "completed event should carry the structured schema version");
  assert_true(has_field(completed_event.fields,
                        "structured_projection_source",
                        "llm_bridge"),
              "completed event should carry the structured projection source");
  assert_true(has_field(completed_event.fields,
                        "projected_node_count",
                        "3"),
              "completed event should carry the projected node count");
  assert_true(has_field(completed_event.fields,
                        "projected_candidate_count",
                        "2"),
              "completed event should carry the projected candidate count");
}

void test_emit_stage_failed_propagates_structured_projection_failure_fields() {
  auto sink = std::make_shared<MockCognitionTelemetrySink>();
  CognitionTelemetry telemetry(dasall::cognition::CognitionConfig{}, sink);

  auto context = make_context();
  context.structured_projection.failure_code = std::string{"invariant"};
  context.structured_projection.source = std::string{"local_fallback"};

  const auto failed = telemetry.emit_stage_failed(context, make_error_info());

  assert_true(failed.emitted,
              "stage failed should emit telemetry across available sinks");
  assert_equal(1, static_cast<int>(sink->log_events.size()),
               "one failed log event should be recorded");
  const auto& failed_event = sink->log_events.back();
  assert_true(has_field(failed_event.fields,
                        "structured_projection_failure_code",
                        "invariant"),
              "failed event should carry the structured projection failure code");
  assert_true(has_field(failed_event.fields,
                        "structured_projection_source",
                        "local_fallback"),
              "failed event should carry the fallback projection source");
}

void test_emit_detail_event_propagates_pipeline_checkpoint_fields() {
  auto sink = std::make_shared<MockCognitionTelemetrySink>();
  CognitionTelemetry telemetry(dasall::cognition::CognitionConfig{}, sink);

  const auto result = telemetry.emit_detail_event(
      "pipeline.checkpoint",
      make_context(),
      {
          TelemetryField{.key = "pipeline", .value = "decision"},
          TelemetryField{.key = "step", .value = "perception"},
          TelemetryField{.key = "outcome", .value = "completed"},
          TelemetryField{.key = "source", .value = "perception_engine"},
          TelemetryField{.key = "elapsed_ms", .value = "12"},
      });

  assert_true(result.emitted,
              "detail event should emit telemetry across available sinks");
  assert_equal(1, static_cast<int>(sink->log_events.size()),
               "one detail log event should be recorded");
  assert_equal(std::string("pipeline.checkpoint"),
               sink->log_events.back().name,
               "detail event should preserve the custom event name");
  assert_true(has_field(sink->log_events.back().fields, "pipeline", "decision"),
              "detail event should carry pipeline name");
  assert_true(has_field(sink->log_events.back().fields, "step", "perception"),
              "detail event should carry step name");
  assert_true(has_field(sink->log_events.back().fields, "source", "perception_engine"),
              "detail event should carry source");
  assert_true(has_field(sink->log_events.back().fields, "elapsed_ms", "12"),
              "detail event should carry elapsed time");
}

  void test_runtime_dependencies_can_inject_custom_telemetry_sink() {
    auto sink = std::make_shared<MockCognitionTelemetrySink>();
    CognitionTelemetry telemetry(
      dasall::cognition::CognitionConfig{},
      make_live_telemetry_sink(dasall::cognition::CognitionRuntimeDependencies{
        .telemetry_sink = sink,
      }));

    const auto result = telemetry.emit_detail_event(
      "replay.trace",
      make_context(),
      {
        TelemetryField{.key = "pipeline", .value = "decision"},
          TelemetryField{
            .key = "serialized_value",
            .value = "raw_prompt=token=topsecret"},
      });

    assert_true(result.emitted,
          "dependency-injected telemetry sink should receive detail events");
    assert_equal(1, static_cast<int>(sink->log_events.size()),
           "custom dependency sink should capture replay detail events");
    assert_true(has_field(sink->log_events.back().fields, "pipeline", "decision"),
          "dependency sink should preserve custom fields");
    assert_true(has_field(sink->log_events.back().fields,
                          "serialized_value",
                          "raw_prompt=[REDACTED]"),
          "dependency sink should still observe redacted payloads");
  }

void test_emit_response_degraded_propagates_route_failure_and_metric_fields() {
  auto sink = std::make_shared<MockCognitionTelemetrySink>();
  CognitionTelemetry telemetry(dasall::cognition::CognitionConfig{}, sink);

  auto context = make_context();
  context.stage = "response";
  context.fallback_used = true;
  context.result_code.reset();

  const auto result = telemetry.emit_response_degraded(
      context,
      dasall::cognition::observability::DegradeTelemetryRecord{
          .fallback_mode = "template_fallback",
          .reason = "llm_bridge_failed",
          .resolved_route = std::string{"mock.route.response"},
          .failure_category = std::string{"adapter_transport"},
          .error_type = std::string{"provider"},
          .payload_excerpt = std::nullopt,
          .omitted_details = {"response_llm_bridge_failed"},
          .audit_refs = {},
      });

  assert_true(result.emitted,
              "response degraded should emit telemetry across available sinks");
  assert_equal(1, static_cast<int>(sink->log_events.size()),
               "one degraded log event should be recorded");
  assert_equal(1, static_cast<int>(sink->metrics.size()),
               "one degraded metric should be recorded");
  assert_true(has_field(sink->log_events.back().fields,
                        "resolved_route",
                        "mock.route.response"),
              "degraded event should carry resolved route");
  assert_true(has_field(sink->log_events.back().fields,
                        "failure_category",
                        "adapter_transport"),
              "degraded event should carry failure category");
  assert_true(has_field(sink->log_events.back().fields, "error_type", "provider"),
              "degraded event should carry error type");
  assert_true(has_field(sink->metrics.back().labels,
                        "resolved_route",
                        "mock.route.response"),
              "degraded metric should carry resolved route label");
  assert_true(has_field(sink->metrics.back().labels,
                        "failure_category",
                        "adapter_transport"),
              "degraded metric should carry failure category label");
  assert_true(has_field(sink->metrics.back().labels, "error_type", "provider"),
              "degraded metric should carry error type label");
}

}  // namespace

int main() {
  try {
    test_emit_stage_started_and_completed_propagates_required_fields();
    test_emit_stage_failed_propagates_structured_projection_failure_fields();
    test_emit_detail_event_propagates_pipeline_checkpoint_fields();
    test_runtime_dependencies_can_inject_custom_telemetry_sink();
    test_emit_response_degraded_propagates_route_failure_and_metric_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
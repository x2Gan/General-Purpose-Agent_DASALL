#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "MockCognitionTelemetrySink.h"
#include "observability/CognitionTelemetry.h"
#include "support/TestAssertions.h"

namespace {

using dasall::cognition::observability::CognitionTelemetry;
using dasall::cognition::observability::StageTelemetryContext;
using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::tests::mocks::MockCognitionTelemetrySink;
using dasall::tests::mocks::MockCognitionTelemetrySinkConfig;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] ErrorInfo make_error_info() {
  return ErrorInfo{
    .failure_type = dasall::contracts::classify_result_code(ResultCode::ValidationFieldMissing),
      .retryable = false,
      .safe_to_replan = false,
      .details = ErrorDetails{
      .code = static_cast<int>(ResultCode::ValidationFieldMissing),
          .message = "schema validation failed",
          .stage = "execution",
      },
      .source_ref = ErrorSourceRefMinimal{
          .ref_type = "cognition.stage_output_validator",
          .ref_id = "decision_kind",
      },
  };
}

void test_sink_failures_do_not_block_stage_failed_emission() {
  auto sink = std::make_shared<MockCognitionTelemetrySink>(
      MockCognitionTelemetrySinkConfig{.fail_log = true});
  CognitionTelemetry telemetry(dasall::cognition::CognitionConfig{}, sink);

  const StageTelemetryContext context{
      .request_id = "req-cog-022-failure",
      .goal_id = "goal-cog-022-failure",
      .profile_id = "desktop_full",
      .stage = "execution",
      .trace_id = "trace-cog-022-failure",
      .model_hint_tier = "standard",
      .fallback_used = false,
      .result_code = static_cast<int>(ResultCode::ValidationFieldMissing),
  };

  const auto result = telemetry.emit_stage_failed(context, make_error_info());

  assert_true(result.emitted,
              "telemetry should remain fail-open when one sink throws");
  assert_equal(1, static_cast<int>(result.diagnostics.size()),
               "one sink failure should produce one diagnostic marker");
  assert_true(result.diagnostics.front() == "telemetry_sink_failure:log",
              "sink failure should identify the failing channel");
  assert_equal(0, static_cast<int>(sink->log_events.size()),
               "failing log sink should not retain an event");
  assert_equal(1, static_cast<int>(sink->metrics.size()),
               "metric sink should still receive the stage failed metric");
  assert_equal(1, static_cast<int>(sink->trace_events.size()),
               "trace sink should still receive the stage failed event");
  assert_equal(1, static_cast<int>(sink->audit_events.size()),
               "audit sink should still receive the stage failed event");
}

}  // namespace

int main() {
  try {
    test_sink_failures_do_not_block_stage_failed_emission();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
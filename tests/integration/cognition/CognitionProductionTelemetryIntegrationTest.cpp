#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "MockCognitionFixture.h"
#include "ObservabilityLiveComposition.h"
#include "audit/AuditService.h"
#include "decision/ActionDecision.h"
#include "logging/LoggingFacade.h"
#include "metrics/MetricsFacade.h"
#include "support/TestAssertions.h"
#include "tracing/TracerProviderImpl.h"

namespace {

using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::runtime_fixture::make_true_integration_policy_snapshot;
using dasall::tests::support::assert_true;

[[nodiscard]] bool export_has_action(const dasall::infra::ExportResult& result,
                                     const std::string& expected_action) {
  for (const auto& record : result.records) {
    if (record.action == expected_action) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool export_contains_token(const dasall::infra::ExportResult& result,
                                         const std::string& token) {
  for (const auto& record : result.records) {
    for (const auto& side_effect : record.side_effects) {
      if (side_effect.find(token) != std::string::npos) {
        return true;
      }
    }
  }

  return false;
}

void test_cognition_production_telemetry_sink_emits_completed_failed_and_degraded_events() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-production-telemetry",
      .trace_id = "trace-cognition-production-telemetry",
      .goal_id = "goal-cognition-production-telemetry",
  });
    fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
    fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);
  const auto snapshot = make_true_integration_policy_snapshot("desktop_full");

    dasall::infra::ObservabilityLiveCompositionOptions options;
    options.profile_id = snapshot->effective_profile_id();
    options.metrics_granularity = snapshot->ops_policy().metrics_granularity;
    options.trace_sample_ratio = snapshot->ops_policy().trace_sample_ratio;
    const auto observability = dasall::infra::compose_live_observability(options);
  assert_true(observability.ok(),
              std::string("cognition production telemetry integration should compose live observability providers: ") +
                  observability.error);

  const auto audit_service =
      std::dynamic_pointer_cast<dasall::infra::audit::AuditService>(observability.audit_logger);
    const auto logger =
      std::dynamic_pointer_cast<dasall::infra::logging::LoggingFacade>(observability.logger);
  const auto metrics_facade =
      std::dynamic_pointer_cast<dasall::infra::metrics::MetricsFacade>(observability.metrics_provider);
  const auto tracer_provider = std::dynamic_pointer_cast<
      dasall::infra::tracing::TracerProviderImpl>(observability.tracer_provider);
    assert_true(logger != nullptr && audit_service != nullptr && metrics_facade != nullptr &&
            tracer_provider != nullptr,
          "cognition production telemetry integration should keep concrete logging, audit, metrics, and trace providers inspectable");

  auto engine = dasall::cognition::create_cognition_engine(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = fixture.llm_manager(),
          .policy_snapshot = snapshot,
        .logger = observability.logger,
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
      });
  assert_true(engine != nullptr,
              "cognition production telemetry integration should create a snapshot-backed engine");

  auto valid_request = fixture.make_decide_request(true);
  const auto valid_result = engine->decide(valid_request);
  assert_true(valid_result.action_decision.has_value() && !valid_result.error_info.has_value(),
              "cognition production telemetry integration should keep a valid decide request successful");

  auto invalid_request = fixture.make_decide_request(true);
  invalid_request.request_id.clear();
  const auto invalid_result = engine->decide(invalid_request);
  assert_true(invalid_result.error_info.has_value(),
              "cognition production telemetry integration should emit a stage failure for invalid decide input");

  auto response_builder = dasall::cognition::create_response_builder(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = fixture.llm_manager(),
          .policy_snapshot = snapshot,
        .logger = observability.logger,
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
      });
  assert_true(response_builder != nullptr,
              "cognition production telemetry integration should create a snapshot-backed response builder");

  auto decision = fixture.make_action_decision(
      dasall::cognition::decision::ActionDecisionKind::DirectResponse);
  decision.response_outline = dasall::cognition::decision::ResponseOutline{
      .summary = "raw_prompt=top-secret api_token=secret-token",
      .key_points = {std::string("telemetry degradation should redact sensitive payloads")},
  };
  auto response_request = fixture.make_response_request(decision);
  response_request.build_hints.prefer_template = true;
  response_request.build_hints.max_summary_chars = 256U;

  const auto response_result = response_builder->build(response_request);
  assert_true(response_result.fallback_used && response_result.agent_result.has_value() &&
                  response_result.agent_result->response_text.has_value(),
              "cognition production telemetry integration should emit a degraded response through template fallback");
  assert_true(response_result.agent_result->response_text->find("[REDACTED]") !=
                  std::string::npos,
              "template fallback should redact sensitive content before degraded telemetry is emitted");

  const auto audit_export = audit_service->export_audit(dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  });
  assert_true(export_has_action(audit_export, "cognition.stage.completed"),
              "production telemetry audit export should include cognition.stage.completed");
  assert_true(export_has_action(audit_export, "cognition.stage.failed"),
              "production telemetry audit export should include cognition.stage.failed");
  assert_true(export_has_action(audit_export, "cognition.response.degraded"),
              "production telemetry audit export should include cognition.response.degraded");
  assert_true(!export_contains_token(audit_export, "top-secret") &&
                  !export_contains_token(audit_export, "secret-token"),
              "production telemetry audit export should not leak redacted degraded payload content");

  assert_true(metrics_facade->record_attempt_count() > 0U,
              "production telemetry integration should record cognition metrics samples");
  assert_true(tracer_provider->tracer_count() > 0U,
              "production telemetry integration should open at least one cognition tracer scope");
  assert_true(logger->dispatched_record_count() >= 3U,
              "production telemetry integration should dispatch cognition logs for completed, failed, and degraded paths");
}

}  // namespace

int main() {
  try {
    test_cognition_production_telemetry_sink_emits_completed_failed_and_degraded_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
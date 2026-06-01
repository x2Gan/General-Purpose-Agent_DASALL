#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "MockCognitionFixture.h"
#include "MockLLMManager.h"
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
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::mocks::StructuredPerceptionPayloadScenario;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::runtime_fixture::make_true_integration_policy_snapshot;
using dasall::tests::support::assert_true;
using dasall::llm::LLMFailureCategory;

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples.push_back(sample);
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://cognition/production-telemetry-recorded");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://cognition/production-telemetry-provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://cognition/production-telemetry-provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://cognition/production-telemetry-provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

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

[[nodiscard]] bool has_metric_identity(
    const std::vector<dasall::infra::metrics::MetricIdentity>& identities,
    const std::string& name,
    const dasall::infra::metrics::MetricType type) {
  return std::any_of(
      identities.begin(),
      identities.end(),
      [&](const dasall::infra::metrics::MetricIdentity& identity) {
        return identity.name == name && identity.type == type;
      });
}

[[nodiscard]] const dasall::infra::metrics::MetricSample* find_metric_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    const std::string& name,
    const std::string& stage,
    const std::string& outcome,
    const std::string& resolved_route = std::string(),
    const std::string& profile = std::string(),
    const std::string& decision_kind = std::string()) {
  const auto match = std::find_if(samples.begin(),
                                  samples.end(),
                                  [&](const dasall::infra::metrics::MetricSample& sample) {
                                    return sample.identity_ref.name == name &&
                                           sample.labels.stage == stage &&
                                           sample.labels.outcome == outcome &&
                                           (profile.empty() ||
                                            sample.labels.profile == profile) &&
                                           (decision_kind.empty() ||
                                            sample.labels.decision_kind == decision_kind) &&
                                           (resolved_route.empty() ||
                                            sample.labels.resolved_route == resolved_route);
                                  });
  return match == samples.end() ? nullptr : &(*match);
}

void test_cognition_production_telemetry_sink_emits_completed_failed_and_degraded_events() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-production-telemetry",
      .trace_id = "trace-cognition-production-telemetry",
      .goal_id = "goal-cognition-production-telemetry",
  });
    fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
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

  auto recording_meter = std::make_shared<RecordingMeter>();
  auto recording_metrics_provider =
      std::make_shared<RecordingMetricsProvider>(recording_meter);

  auto engine = dasall::cognition::create_cognition_engine(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = fixture.llm_manager(),
          .policy_snapshot = snapshot,
        .logger = observability.logger,
          .audit_logger = observability.audit_logger,
          .metrics_provider = recording_metrics_provider,
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
          .metrics_provider = recording_metrics_provider,
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

    MockCognitionFixture response_failure_fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-telemetry-response-failure",
      .trace_id = "trace-cognition-telemetry-response-failure",
      .goal_id = "goal-cognition-telemetry-response-failure",
    });
    response_failure_fixture.llm_manager()->set_stage_result(
      "response",
      MockLLMManager::make_failure_result(
        dasall::contracts::ResultCode::ProviderTimeout,
        "response bridge intentionally unavailable for telemetry verification",
        LLMFailureCategory::AdapterTransport,
        "mock.route.response",
        response_failure_fixture.options().request_id));
    auto response_failure_builder = dasall::cognition::create_response_builder(
      dasall::cognition::CognitionConfig{},
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = response_failure_fixture.llm_manager(),
        .policy_snapshot = nullptr,
        .logger = observability.logger,
        .audit_logger = observability.audit_logger,
        .metrics_provider = recording_metrics_provider,
        .tracer_provider = observability.tracer_provider,
      });
    auto response_failure_request = response_failure_fixture.make_response_request();
    response_failure_request.build_hints.prefer_template = false;
    response_failure_request.build_hints.allow_template_fallback = true;

    const auto response_failure_result = response_failure_builder->build(response_failure_request);
    assert_true(response_failure_result.fallback_used &&
            response_failure_result.agent_result.has_value() &&
            !response_failure_result.error_info.has_value(),
          "cognition production telemetry integration should keep response bridge failures on the degraded fallback path");

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
    assert_true(export_contains_token(audit_export, "field:resolved_route=mock.route.response") &&
            export_contains_token(audit_export,
                      "field:failure_category=adapter_transport") &&
            export_contains_token(audit_export, "field:error_type=provider"),
          "production telemetry audit export should preserve response degraded route and failure classification fields");
  assert_true(!export_contains_token(audit_export, "top-secret") &&
                  !export_contains_token(audit_export, "secret-token"),
              "production telemetry audit export should not leak redacted degraded payload content");

  assert_true(!recording_meter->recorded_samples.empty(),
              "production telemetry integration should emit cognition metrics into the injected metrics provider");
    assert_true(recording_metrics_provider->last_scope.name == "cognition" &&
            recording_metrics_provider->last_scope.version == "v1",
          "production telemetry integration should request the cognition meter scope when registering semantic metrics");
    assert_true(has_metric_identity(recording_meter->created_identities,
                    "cognition_stage_latency_ms",
                    dasall::infra::metrics::MetricType::Histogram),
          "production telemetry integration should register cognition_stage_latency_ms as a histogram");
    assert_true(has_metric_identity(recording_meter->created_identities,
                    "cognition_stage_total",
                    dasall::infra::metrics::MetricType::Counter),
          "production telemetry integration should register cognition_stage_total as a counter");
    assert_true(has_metric_identity(recording_meter->created_identities,
                    "cognition_action_decision_total",
                    dasall::infra::metrics::MetricType::Counter),
          "production telemetry integration should register cognition_action_decision_total as a counter");

    const auto* stage_latency_success = find_metric_sample(recording_meter->recorded_samples,
                               "cognition_stage_latency_ms",
                               "execution",
                               "success",
                               std::string(),
                               "desktop_full",
                               "DirectResponse");
    assert_true(stage_latency_success != nullptr,
          "production telemetry integration should emit cognition_stage_latency_ms with stage, result, profile, and decision_kind labels on successful decide output");

    const auto* stage_total_success = find_metric_sample(recording_meter->recorded_samples,
                               "cognition_stage_total",
                               "execution",
                               "success",
                               std::string(),
                               "desktop_full",
                               "DirectResponse");
    assert_true(stage_total_success != nullptr,
          "production telemetry integration should emit cognition_stage_total with stage, result, profile, and decision_kind labels on successful decide output");

    const auto* action_decision_success = find_metric_sample(recording_meter->recorded_samples,
                                 "cognition_action_decision_total",
                                 "execution",
                                 "success",
                                 std::string(),
                                 "desktop_full",
                                 "DirectResponse");
    assert_true(action_decision_success != nullptr,
          "production telemetry integration should emit cognition_action_decision_total with stage, result, profile, and decision_kind labels on successful decide output");

    const auto* stage_total_failure = find_metric_sample(recording_meter->recorded_samples,
                               "cognition_stage_total",
                               "execution",
                               "failure",
                               std::string(),
                               "desktop_full");
    assert_true(stage_total_failure != nullptr &&
            stage_total_failure->labels.decision_kind == "none",
          "production telemetry integration should materialize decision_kind on failed stage totals even when no decision was produced");

    const auto* stage_total_degraded = find_metric_sample(recording_meter->recorded_samples,
                              "cognition_stage_total",
                              "response",
                              "degraded",
                              std::string(),
                              "desktop_full");
    assert_true(stage_total_degraded != nullptr &&
            stage_total_degraded->labels.decision_kind == "none",
          "production telemetry integration should materialize decision_kind on degraded stage totals for template fallback output");

  const auto* degraded_metric = find_metric_sample(recording_meter->recorded_samples,
                                                   "cognition_response_degraded_total",
                                                   "response",
                                                   "degraded",
                                                   "mock.route.response");
  assert_true(degraded_metric != nullptr,
              "production telemetry integration should emit a response degraded metric sample carrying the llm route");
  assert_true(degraded_metric->labels.failure_category == "adapter_transport",
              "production telemetry integration should preserve failure_category on the degraded response metric");
  assert_true(degraded_metric->labels.error_type == "provider",
              "production telemetry integration should preserve error_type on the degraded response metric");
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
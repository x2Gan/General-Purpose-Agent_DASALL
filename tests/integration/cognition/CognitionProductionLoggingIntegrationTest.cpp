#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "MockCognitionFixture.h"
#include "MockLLMManager.h"
#include "ObservabilityLiveComposition.h"
#include "decision/ActionDecision.h"
#include "logging/ILogConfigurator.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::mocks::StructuredPerceptionPayloadScenario;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::runtime_fixture::make_true_integration_policy_snapshot;
using dasall::tests::support::assert_true;
using dasall::llm::LLMFailureCategory;

class TempLogRoot {
 public:
  explicit TempLogRoot(const std::string& stem)
      : path_(fs::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                                                 .count()))) {
    fs::create_directories(path_);
  }

  ~TempLogRoot() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

[[nodiscard]] dasall::infra::config::TypedConfig make_entry(
    std::string key_path,
    dasall::infra::config::ConfigValueType value_type,
    std::string serialized_value,
    dasall::infra::config::ConfigSourceKind source_kind,
    std::string source_id) {
  return dasall::infra::config::TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string(dasall::infra::config::kConfigSchemaVersionV1),
      .source_kind = source_kind,
      .source_id = std::move(source_id),
      .secret_backed = false,
  };
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] bool has_attr(const dasall::infra::LogEvent::AttributeMap& attrs,
                            const std::string& key) {
  return attrs.find(key) != attrs.end();
}

void test_cognition_production_logging_live_composition_persists_redacted_events() {
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LoggingFacade;

  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-production-logging",
      .trace_id = "trace-cognition-production-logging",
      .goal_id = "goal-cognition-production-logging",
  });
    fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);
  const auto snapshot = make_true_integration_policy_snapshot("desktop_full");

  TempLogRoot log_root("dasall-cognition-production-logging");
  const auto runtime_log_path = log_root.path() / "runtime.log";
  const std::string profile_source = "profiles/desktop_full/runtime_policy.yaml";
  const std::string deploy_source = "deploy://site/logging.yaml";

  ObservabilityLiveCompositionOptions options;
  options.profile_id = snapshot->effective_profile_id();
  options.metrics_granularity = snapshot->ops_policy().metrics_granularity;
  options.trace_sample_ratio = snapshot->ops_policy().trace_sample_ratio;
  options.logging_config_entries = {
      make_entry("infra.logging.async.enabled",
                 ConfigValueType::Boolean,
                 "false",
                 ConfigSourceKind::Profile,
                 profile_source),
      make_entry("infra.logging.format",
                 ConfigValueType::String,
                 "json_line",
                 ConfigSourceKind::DeploymentOverride,
                 deploy_source),
      make_entry("infra.logging.file.path",
                 ConfigValueType::String,
                 runtime_log_path.string(),
                 ConfigSourceKind::DeploymentOverride,
                 deploy_source),
  };

  const auto observability = compose_live_observability(options);
  assert_true(observability.ok(),
              std::string("cognition production logging integration should compose live observability providers: ") +
                  observability.error);

  const auto logger =
      std::dynamic_pointer_cast<LoggingFacade>(observability.logger);
  assert_true(logger != nullptr,
              "cognition production logging integration should keep the concrete logger inspectable");

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
              "cognition production logging integration should create a snapshot-backed engine");

  auto valid_request = fixture.make_decide_request(true);
  const auto valid_result = engine->decide(valid_request);
  assert_true(valid_result.action_decision.has_value() && !valid_result.error_info.has_value(),
              "cognition production logging integration should keep a valid decide request successful");

  auto invalid_request = fixture.make_decide_request(true);
  invalid_request.request_id.clear();
  const auto invalid_result = engine->decide(invalid_request);
  assert_true(invalid_result.error_info.has_value(),
              "cognition production logging integration should emit a stage failure for invalid decide input");

    MockCognitionFixture bridge_failure_fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-bridge-failure",
      .trace_id = "trace-cognition-bridge-failure",
      .goal_id = "goal-cognition-bridge-failure",
    });
    bridge_failure_fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
    bridge_failure_fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
    bridge_failure_fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_failure_result(
        dasall::contracts::ResultCode::ProviderTimeout,
        "execution bridge intentionally unavailable for runtime.log verification",
        LLMFailureCategory::ProviderProtocol,
        "mock.route.execution",
        bridge_failure_fixture.options().request_id));

    auto bridge_failure_engine = dasall::cognition::create_cognition_engine(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = bridge_failure_fixture.llm_manager(),
        .policy_snapshot = snapshot,
        .logger = observability.logger,
        .audit_logger = observability.audit_logger,
        .metrics_provider = observability.metrics_provider,
        .tracer_provider = observability.tracer_provider,
      });
    auto bridge_failure_request = bridge_failure_fixture.make_decide_request(true);
    bridge_failure_request.execution_hints.degraded_path_allowed = false;
    const auto bridge_failure_result = bridge_failure_engine->decide(bridge_failure_request);
    assert_true(bridge_failure_result.error_info.has_value(),
          "cognition production logging integration should surface a fail-closed execution bridge provider failure");

    MockCognitionFixture schema_fallback_fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-schema-fallback",
      .trace_id = "trace-cognition-schema-fallback",
      .goal_id = "goal-cognition-schema-fallback",
    });
    schema_fallback_fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
    schema_fallback_fixture.stage_structured_planning_result(
      StructuredPlanningPayloadScenario::SchemaInvalidActionKindHint);
      schema_fallback_fixture.llm_manager()->set_stage_result(
        "execution",
        MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "execution bridge intentionally unavailable to keep schema fallback on the local reasoner path",
          LLMFailureCategory::ProviderProtocol,
          "mock.route.execution",
          schema_fallback_fixture.options().request_id));

      auto schema_fallback_engine = dasall::cognition::create_cognition_engine(
        dasall::cognition::CognitionConfig{},
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = schema_fallback_fixture.llm_manager(),
          .policy_snapshot = nullptr,
        .logger = observability.logger,
        .audit_logger = observability.audit_logger,
        .metrics_provider = observability.metrics_provider,
        .tracer_provider = observability.tracer_provider,
      });
    const auto schema_fallback_result =
      schema_fallback_engine->decide(schema_fallback_fixture.make_decide_request(true));
    assert_true(schema_fallback_result.action_decision.has_value() &&
            !schema_fallback_result.error_info.has_value(),
          "cognition production logging integration should keep schema-invalid planning payloads on the degraded local fallback path");

    MockCognitionFixture reflection_failure_fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-reflection-bridge-failure",
      .trace_id = "trace-cognition-reflection-bridge-failure",
      .goal_id = "goal-cognition-reflection-bridge-failure",
    });
    reflection_failure_fixture.llm_manager()->set_stage_result(
      "reflection",
      MockLLMManager::make_failure_result(
        dasall::contracts::ResultCode::ProviderTimeout,
        "reflection bridge intentionally unavailable for runtime.log verification",
        LLMFailureCategory::ProviderProtocol,
        "mock.route.reflection",
        reflection_failure_fixture.options().request_id));

      auto reflection_failure_engine = dasall::cognition::create_cognition_engine(
        dasall::cognition::CognitionConfig{},
      dasall::cognition::CognitionRuntimeDependencies{
        .llm_manager = reflection_failure_fixture.llm_manager(),
          .policy_snapshot = nullptr,
        .logger = observability.logger,
        .audit_logger = observability.audit_logger,
        .metrics_provider = observability.metrics_provider,
        .tracer_provider = observability.tracer_provider,
      });
    auto reflection_failure_request = reflection_failure_fixture.make_reflection_request();
    reflection_failure_request.execution_hints.degraded_path_allowed = false;
    const auto reflection_failure_result =
      reflection_failure_engine->reflect(reflection_failure_request);
    assert_true(reflection_failure_result.error_info.has_value(),
          "cognition production logging integration should surface a fail-closed reflection bridge provider failure");

    MockCognitionFixture response_failure_fixture(MockCognitionFixtureOptions{
      .request_id = "req-cognition-response-bridge-failure",
      .trace_id = "trace-cognition-response-bridge-failure",
      .goal_id = "goal-cognition-response-bridge-failure",
    });
    response_failure_fixture.llm_manager()->set_stage_result(
      "response",
      MockLLMManager::make_failure_result(
        dasall::contracts::ResultCode::ProviderTimeout,
        "response bridge intentionally unavailable for runtime.log verification",
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
        .metrics_provider = observability.metrics_provider,
        .tracer_provider = observability.tracer_provider,
      });
    auto response_failure_request = response_failure_fixture.make_response_request();
    response_failure_request.build_hints.prefer_template = false;
    response_failure_request.build_hints.allow_template_fallback = true;
    const auto response_failure_result =
      response_failure_builder->build(response_failure_request);
    assert_true(response_failure_result.fallback_used &&
            response_failure_result.agent_result.has_value() &&
            !response_failure_result.error_info.has_value(),
          "cognition production logging integration should keep response bridge provider failures on the degraded template fallback path");

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
              "cognition production logging integration should create a snapshot-backed response builder");

  auto decision = fixture.make_action_decision(
      dasall::cognition::decision::ActionDecisionKind::DirectResponse);
  decision.response_outline = dasall::cognition::decision::ResponseOutline{
      .summary = "raw_prompt=top-secret api_token=secret-token payload_excerpt=secret-payload",
      .key_points = {std::string("cognition production logging should redact sensitive payloads")},
  };
  auto response_request = fixture.make_response_request(decision);
  response_request.build_hints.prefer_template = true;
  response_request.build_hints.max_summary_chars = 256U;

  const auto response_result = response_builder->build(response_request);
  assert_true(response_result.fallback_used && response_result.agent_result.has_value() &&
                  response_result.agent_result->response_text.has_value(),
              "cognition production logging integration should emit a degraded response through template fallback");
  assert_true(response_result.agent_result->response_text->find("[REDACTED]") !=
                  std::string::npos,
              "template fallback should redact sensitive content before degraded logging is emitted");

  assert_true(logger->flush(LogFlushDeadline{.timeout_ms = 250}).ok,
              "cognition production logging integration should flush the live logger before inspecting runtime.log");
  assert_true(logger->has_last_dispatched_event(),
              "cognition production logging integration should keep the degraded event as the last dispatched log");

  const auto& last_event = logger->last_dispatched_event();
  assert_true(last_event.module == "cognition",
              "cognition production logging integration should dispatch cognition module log events");
  assert_true(has_attr(last_event.attrs, "event_name") &&
                  last_event.attrs.at("event_name") == "response.degraded",
              "cognition production logging integration should keep response.degraded as the final log event");
  assert_true(has_attr(last_event.attrs, "fallback_mode") &&
                  !has_attr(last_event.attrs, "payload_excerpt") &&
                  !has_attr(last_event.attrs, "response_summary") &&
                  !has_attr(last_event.attrs, "clarification_question") &&
                  !has_attr(last_event.attrs, "candidate_scores"),
              "cognition production logging integration should keep only allowlisted degraded attrs in the live log event");

  const auto runtime_log_text = read_text_file(runtime_log_path);
  assert_true(runtime_log_text.find("cognition stage.completed") != std::string::npos &&
                  runtime_log_text.find("cognition stage.failed") != std::string::npos &&
                  runtime_log_text.find("cognition response.degraded") != std::string::npos,
              "cognition production logging integration should persist completed, failed, and degraded cognition events into runtime.log");
    assert_true(runtime_log_text.find("\"event_name\":\"pipeline.checkpoint\"") != std::string::npos &&
            runtime_log_text.find("\"pipeline\":\"decision\"") != std::string::npos &&
            runtime_log_text.find("\"pipeline\":\"reflection\"") != std::string::npos &&
            runtime_log_text.find("\"step\":\"perception\"") != std::string::npos &&
            runtime_log_text.find("\"step\":\"planning\"") != std::string::npos &&
            runtime_log_text.find("\"step\":\"execution\"") != std::string::npos &&
            runtime_log_text.find("\"step\":\"reflection\"") != std::string::npos &&
            runtime_log_text.find("\"step\":\"mode_selection\"") != std::string::npos &&
            runtime_log_text.find("\"step\":\"build\"") != std::string::npos &&
            runtime_log_text.find("\"mode\":\"template_fallback\"") != std::string::npos,
          "cognition production logging integration should persist detailed pipeline checkpoints for decision, reflection, and response control points");
    assert_true(runtime_log_text.find("\"source\":\"llm_bridge\"") != std::string::npos &&
            runtime_log_text.find("\"structured_projection_failure_code\":\"provider\"") !=
              std::string::npos &&
            runtime_log_text.find("\"structured_projection_failure_code\":\"schema\"") !=
              std::string::npos &&
            runtime_log_text.find("\"fallback_allowed\":\"false\"") != std::string::npos &&
            runtime_log_text.find("\"fallback_allowed\":\"true\"") != std::string::npos,
          "cognition production logging integration should persist llm bridge and structured projection failure metadata into pipeline checkpoints");
  assert_true(runtime_log_text.find("\"resolved_route\":\"mock.route.response\"") != std::string::npos &&
                  runtime_log_text.find("\"failure_category\":\"adapter_transport\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"error_type\":\"provider\"") != std::string::npos,
              "cognition production logging integration should persist response bridge route and failure classification into runtime.log");
  assert_true(runtime_log_text.find("\"module\":\"cognition\"") != std::string::npos &&
                  runtime_log_text.find("\"fallback_mode\"") != std::string::npos,
              "cognition production logging integration should persist structured cognition attrs into runtime.log");
  assert_true(runtime_log_text.find("top-secret") == std::string::npos &&
                  runtime_log_text.find("secret-token") == std::string::npos &&
                  runtime_log_text.find("payload_excerpt") == std::string::npos &&
                  runtime_log_text.find("response_summary") == std::string::npos &&
                  runtime_log_text.find("clarification_question") == std::string::npos &&
                  runtime_log_text.find("candidate_scores") == std::string::npos,
              "cognition production logging integration should not leak raw prompt, token, or forbidden attrs into runtime.log");
}

}  // namespace

int main() {
  try {
    test_cognition_production_logging_live_composition_persists_redacted_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
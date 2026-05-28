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
#include "ObservabilityLiveComposition.h"
#include "decision/ActionDecision.h"
#include "logging/ILogConfigurator.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::runtime_fixture::make_true_integration_policy_snapshot;
using dasall::tests::support::assert_true;

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
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CognitionConfig.h"
#include "CognitionDependencies.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "decision/ActionDecision.h"
#include "observability/CognitionReplayTraceRecorder.h"
#include "support/TestAssertions.h"

#include "../../mocks/include/MockCognitionFixture.h"
#include "../../mocks/include/MockLLMManager.h"

namespace {

namespace fs = std::filesystem;

using dasall::cognition::CognitionConfig;
using dasall::cognition::CognitionRuntimeDependencies;
using dasall::cognition::create_cognition_engine;
using dasall::cognition::create_response_builder;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::observability::ReplayFailureSampleRule;
using dasall::cognition::observability::ReplayFailureSamplingConfig;
using dasall::cognition::observability::ReplayTraceRecorderConfig;
using dasall::cognition::observability::make_replay_trace_recorder;
using dasall::cognition::observability::replay_trace_file_name;
using dasall::llm::ILLMManager;
using dasall::llm::LLMFailureCategory;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPerceptionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::support::assert_true;

constexpr std::string_view kReplayProfile = "build-ci/replay";

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] fs::path make_output_dir(std::string_view case_id) {
  const auto output_dir =
      fs::temp_directory_path() /
      (std::string{"dasall-cognition-failure-sampling-"} + std::string(case_id));
  std::error_code error;
  fs::remove_all(output_dir, error);
  fs::create_directories(output_dir, error);
  return output_dir;
}

[[nodiscard]] fs::path failure_sample_dir(const fs::path& output_dir,
                                          std::string_view category) {
  return output_dir / "failure_samples" / std::string(category);
}

void assert_sampled_trace_exists(const fs::path& output_dir,
                                 std::string_view category,
                                 std::string_view request_id,
                                 std::string_view stage,
                                 std::string_view event_name) {
  const auto trace_path =
      failure_sample_dir(output_dir, category) /
      replay_trace_file_name(request_id, stage, event_name);
  assert_true(fs::exists(trace_path),
              "sampled replay trace should exist: " + trace_path.string());
}

[[nodiscard]] CognitionRuntimeDependencies make_dependencies(
    const fs::path& output_dir,
    ReplayFailureSamplingConfig sampling_config,
    std::shared_ptr<ILLMManager> llm_manager = nullptr) {
  return CognitionRuntimeDependencies{
      .llm_manager = std::move(llm_manager),
      .telemetry_sink = make_replay_trace_recorder(ReplayTraceRecorderConfig{
          .output_dir = output_dir.string(),
          .enabled_profile_id = std::string{kReplayProfile},
          .failure_sampling = std::move(sampling_config),
      }),
  };
}

[[nodiscard]] ReplayFailureSamplingConfig make_sampling_config(
        ReplayFailureSampleRule cognition_schema_violation = {},
        ReplayFailureSampleRule reflection_abort_safe = {},
        ReplayFailureSampleRule response_fallback_used = {}) {
    ReplayFailureSamplingConfig config;
    config.cognition_schema_violation = std::move(cognition_schema_violation);
    config.reflection_abort_safe = std::move(reflection_abort_safe);
    config.response_fallback_used = std::move(response_fallback_used);
    return config;
}

[[nodiscard]] std::string make_reflection_payload(
    const MockCognitionFixtureOptions& options,
    const std::string& decision_kind,
    const std::string& rationale,
    const std::string& hint_ref) {
  return std::string{"{"}
      + "\"schema_version\":\"cognition.reflection.v1\","
      + "\"request_id\":\"" + options.request_id + "\","
      + "\"decision_kind\":\"" + decision_kind + "\","
      + "\"rationale\":\"" + rationale + "\","
      + "\"goal_id\":\"" + options.goal_id + "\","
      + "\"confidence\":0.91,"
      + "\"relevant_observation_refs\":[\"" + options.observation_id + "\"],"
      + "\"hint_ref\":\"" + hint_ref + "\","
      + "\"created_at\":1712746800000,"
      + "\"tags\":[\"cognition\",\"reflection\",\"sampling\"]}"
      ;
}

void test_schema_violation_failure_sampling_copies_request_corpus() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-failure-sampling-schema-violation",
      .trace_id = "trace-failure-sampling-schema-violation",
      .profile_id = std::string{kReplayProfile},
      .goal_id = "goal-failure-sampling-schema-violation",
  });
  fixture.stage_structured_perception_result(
      StructuredPerceptionPayloadScenario::ValidActionDecision);
  fixture.stage_structured_planning_result(
      StructuredPlanningPayloadScenario::SchemaInvalidActionKindHint);
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "execution bridge intentionally unavailable to keep sampling on the degraded path",
          LLMFailureCategory::ProviderProtocol,
          "mock.route.execution",
          fixture.options().request_id));

  const auto output_dir = make_output_dir("schema-violation");
  auto engine = create_cognition_engine(
      CognitionConfig{},
      make_dependencies(output_dir,
                        make_sampling_config(
                            ReplayFailureSampleRule{
                                .enabled = true,
                                .sample_rate = 1.0,
                            }),
                        fixture.llm_manager_port()));

  const auto result = engine->decide(fixture.make_decide_request(true));

  assert_true(result.action_decision.has_value(),
              "schema violation sampling case should still yield a bounded decision");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.schema_violation:planning"),
              "schema violation sampling case should preserve the planning schema violation diagnostic");

  assert_sampled_trace_exists(output_dir,
                              "cognition.schema_violation",
                              fixture.options().request_id,
                              "execution",
                              "replay.trace.decide.request");
  assert_sampled_trace_exists(output_dir,
                              "cognition.schema_violation",
                              fixture.options().request_id,
                              "planning",
                              "replay.trace.decide.bridge_payload");
  assert_sampled_trace_exists(output_dir,
                              "cognition.schema_violation",
                              fixture.options().request_id,
                              "execution",
                              "replay.trace.decide.result");
}

void test_reflection_abort_safe_failure_sampling_copies_request_corpus() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-failure-sampling-reflection-abort-safe",
      .trace_id = "trace-failure-sampling-reflection-abort-safe",
      .profile_id = std::string{kReplayProfile},
      .goal_id = "goal-failure-sampling-reflection-abort-safe",
      .observation_id = "obs-failure-sampling-reflection-abort-safe",
  });
  fixture.llm_manager()->set_structured_stage_payload(
      "reflection",
      make_reflection_payload(
          fixture.options(),
          "AbortSafe",
          "reflection sampling should preserve abort-safe evidence for offline review",
          "hint:reflection:abort_safe"),
      fixture.options().request_id);

  const auto output_dir = make_output_dir("reflection-abort-safe");
  auto engine = create_cognition_engine(
      CognitionConfig{},
      make_dependencies(output_dir,
                        make_sampling_config(
                            {},
                            ReplayFailureSampleRule{
                                .enabled = true,
                                .sample_rate = 1.0,
                            }),
                        fixture.llm_manager_port()));
  auto request = fixture.make_reflection_request(
      fixture.make_observation(false,
                               R"({"status":"failed","summary":"tool execution diverged"})"));
  request.execution_hints.degraded_path_allowed = false;

  const auto result = engine->reflect(request);

  assert_true(result.reflection_decision.has_value(),
              "reflection abort-safe sampling case should return a reflection decision");
  assert_true(result.reflection_decision->decision_kind.has_value() &&
                  *result.reflection_decision->decision_kind ==
                      dasall::contracts::ReflectionDecisionKind::AbortSafe,
              "reflection abort-safe sampling case should preserve the abort-safe decision kind");

  assert_sampled_trace_exists(output_dir,
                              "reflection.abort_safe",
                              fixture.options().request_id,
                              "reflection",
                              "replay.trace.reflect.request");
  assert_sampled_trace_exists(output_dir,
                              "reflection.abort_safe",
                              fixture.options().request_id,
                              "reflection",
                              "replay.trace.reflect.bridge_payload");
  assert_sampled_trace_exists(output_dir,
                              "reflection.abort_safe",
                              fixture.options().request_id,
                              "reflection",
                              "replay.trace.reflect.result");
}

void test_response_fallback_failure_sampling_copies_request_corpus() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-failure-sampling-response-fallback",
      .trace_id = "trace-failure-sampling-response-fallback",
      .profile_id = std::string{kReplayProfile},
      .goal_id = "goal-failure-sampling-response-fallback",
  });

  const auto output_dir = make_output_dir("response-fallback");
  auto builder = create_response_builder(
      CognitionConfig{},
      make_dependencies(output_dir,
                        make_sampling_config(
                            {},
                            {},
                            ReplayFailureSampleRule{
                                .enabled = true,
                                .sample_rate = 1.0,
                            })));
  auto request = fixture.make_response_request(
      fixture.make_action_decision(ActionDecisionKind::ConvergeSafe));
  request.latest_observation.reset();
  request.build_hints.allow_template_fallback = true;

  const auto result = builder->build(request);

  assert_true(result.fallback_used,
              "response fallback sampling case should stay on the fallback path");
  assert_true(result.agent_result.has_value(),
              "response fallback sampling case should still materialize an AgentResult");

  assert_sampled_trace_exists(output_dir,
                              "response.fallback_used",
                              fixture.options().request_id,
                              "response",
                              "replay.trace.build.request");
  assert_sampled_trace_exists(output_dir,
                              "response.fallback_used",
                              fixture.options().request_id,
                              "response",
                              "replay.trace.build.result");
}

void test_failure_sampling_zero_rate_skips_corpus_copy() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-failure-sampling-zero-rate",
      .trace_id = "trace-failure-sampling-zero-rate",
      .profile_id = std::string{kReplayProfile},
      .goal_id = "goal-failure-sampling-zero-rate",
  });

  const auto output_dir = make_output_dir("zero-rate");
  auto builder = create_response_builder(
      CognitionConfig{},
      make_dependencies(output_dir,
                        make_sampling_config(
                            {},
                            {},
                            ReplayFailureSampleRule{
                                .enabled = true,
                                .sample_rate = 0.0,
                            })));
  auto request = fixture.make_response_request(
      fixture.make_action_decision(ActionDecisionKind::ConvergeSafe));
  request.latest_observation.reset();
  request.build_hints.allow_template_fallback = true;

  const auto result = builder->build(request);

  assert_true(result.fallback_used,
              "zero-rate sampling case should still run the fallback path");
  assert_true(!fs::exists(failure_sample_dir(output_dir, "response.fallback_used")),
              "zero sample rate should suppress failure corpus copies");
}

}  // namespace

int main() {
  try {
    test_schema_violation_failure_sampling_copies_request_corpus();
    test_reflection_abort_safe_failure_sampling_copies_request_corpus();
    test_response_fallback_failure_sampling_copies_request_corpus();
    test_failure_sampling_zero_rate_skips_corpus_copy();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
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
#include "tests/mocks/include/MockCognitionFixture.h"
#include "tests/mocks/include/MockLLMManager.h"

#ifndef DASALL_COGNITION_REPLAY_DATA_DIR
#define DASALL_COGNITION_REPLAY_DATA_DIR \
  "/home/gangan/DASALL/tests/data/cognition/replay"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::cognition::CognitionConfig;
using dasall::cognition::CognitionRuntimeDependencies;
using dasall::cognition::IResponseBuilder;
using dasall::cognition::create_cognition_engine;
using dasall::cognition::create_response_builder;
using dasall::cognition::decision::ActionDecisionKind;
using dasall::cognition::observability::ReplayTraceRecorderConfig;
using dasall::cognition::observability::make_replay_trace_recorder;
using dasall::cognition::observability::replay_trace_file_name;
using dasall::contracts::AgentResultStatus;
using dasall::llm::ILLMManager;
using dasall::llm::LLMFailureCategory;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
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

[[nodiscard]] fs::path golden_root() {
  return fs::path{DASALL_COGNITION_REPLAY_DATA_DIR};
}

[[nodiscard]] fs::path make_output_dir(std::string_view case_id) {
  const auto output_dir =
      fs::temp_directory_path() /
      (std::string{"dasall-cognition-replay-"} + std::string(case_id));
  std::error_code error;
  fs::remove_all(output_dir, error);
  fs::create_directories(output_dir, error);
  return output_dir;
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open replay trace file: " + path.string());
  }

  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] std::string normalize_text(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

void assert_trace_matches(const fs::path& output_dir,
                          std::string_view request_id,
                          std::string_view stage,
                          std::string_view event_name) {
  const auto file_name = replay_trace_file_name(request_id, stage, event_name);
  const auto expected_path = golden_root() / file_name;
  const auto actual_path = output_dir / file_name;
  assert_true(fs::exists(expected_path),
              "golden replay trace should exist: " + expected_path.string());
  assert_true(fs::exists(actual_path),
              "recorded replay trace should exist: " + actual_path.string());

  const auto expected = normalize_text(read_text(expected_path));
  const auto actual = normalize_text(read_text(actual_path));
  if (expected != actual) {
    throw std::runtime_error("replay trace mismatch: expected " +
                             expected_path.string() + " actual " + actual_path.string());
  }
}

[[nodiscard]] CognitionRuntimeDependencies make_dependencies(
    const fs::path& output_dir,
    std::shared_ptr<ILLMManager> llm_manager = nullptr) {
  return CognitionRuntimeDependencies{
      .llm_manager = std::move(llm_manager),
      .telemetry_sink = make_replay_trace_recorder(ReplayTraceRecorderConfig{
          .output_dir = output_dir.string(),
          .enabled_profile_id = std::string{kReplayProfile},
      }),
  };
}

void test_decide_replay_trace_matches_golden_direct_response() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-replay-decide-direct",
      .trace_id = "trace-replay-decide-direct",
      .profile_id = std::string{kReplayProfile},
      .selected_node_id = "bridge-plan-node",
      .response_text = "bridge-authored direct response summary",
  });
  fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);

  const auto output_dir = make_output_dir("decide-direct");
  auto engine = create_cognition_engine(
      CognitionConfig{},
      make_dependencies(output_dir, fixture.llm_manager_port()));

  const auto result = engine->decide(fixture.make_decide_request(true));

  assert_true(result.action_decision.has_value(),
              "direct replay case should yield an action decision");
  assert_true(result.action_decision->decision_kind == ActionDecisionKind::DirectResponse,
              "direct replay case should preserve the authoritative direct-response decision");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.projected_action_decision"),
              "direct replay case should keep the execution projection diagnostic");

  assert_trace_matches(output_dir,
                       "req-replay-decide-direct",
                       "execution",
                       "replay.trace.decide.request");
  assert_trace_matches(output_dir,
                       "req-replay-decide-direct",
                       "planning",
                       "replay.trace.decide.bridge_payload");
  assert_trace_matches(output_dir,
                       "req-replay-decide-direct",
                       "execution",
                       "replay.trace.decide.bridge_payload");
  assert_trace_matches(output_dir,
                       "req-replay-decide-direct",
                       "execution",
                       "replay.trace.decide.result");
}

void test_decide_replay_trace_matches_golden_planning_fallback() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-replay-decide-planning-fallback",
      .trace_id = "trace-replay-decide-planning-fallback",
      .profile_id = std::string{kReplayProfile},
      .selected_node_id = "fallback-plan-node",
  });
  fixture.stage_structured_planning_result(
      StructuredPlanningPayloadScenario::SchemaInvalidActionKindHint);
  fixture.llm_manager()->set_stage_result(
      "execution",
      MockLLMManager::make_failure_result(
          dasall::contracts::ResultCode::ProviderTimeout,
          "execution bridge intentionally unavailable to keep replay on the degraded path",
          LLMFailureCategory::ProviderProtocol,
          "mock.route.execution",
          fixture.options().request_id));

  const auto output_dir = make_output_dir("decide-planning-fallback");
  auto engine = create_cognition_engine(
      CognitionConfig{},
      make_dependencies(output_dir, fixture.llm_manager_port()));

  const auto result = engine->decide(fixture.make_decide_request(true));

  assert_true(result.action_decision.has_value(),
              "planning fallback replay case should still return a bounded decision");
  assert_true(contains_value(result.diagnostics, "decision_pipeline.degraded"),
              "planning fallback replay case should preserve the degraded diagnostic");
  assert_true(contains_value(result.diagnostics,
                             "structured_projection.local_fallback:planning"),
              "planning fallback replay case should keep the local-fallback marker");

  assert_trace_matches(output_dir,
                       "req-replay-decide-planning-fallback",
                       "execution",
                       "replay.trace.decide.request");
  assert_trace_matches(output_dir,
                       "req-replay-decide-planning-fallback",
                       "planning",
                       "replay.trace.decide.bridge_payload");
  assert_trace_matches(output_dir,
                       "req-replay-decide-planning-fallback",
                       "execution",
                       "replay.trace.decide.result");
}

void test_reflect_replay_trace_matches_golden_continue_path() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-replay-reflect-continue",
      .trace_id = "trace-replay-reflect-continue",
      .profile_id = std::string{kReplayProfile},
      .observation_payload = R"({"status":"ok","summary":"verified evidence returned"})",
  });

  const auto output_dir = make_output_dir("reflect-continue");
  auto engine = create_cognition_engine(CognitionConfig{}, make_dependencies(output_dir));

  const auto result = engine->reflect(
      fixture.make_reflection_request(
          fixture.make_observation(true,
                                   R"({"status":"ok","summary":"verified evidence returned"})")));

  assert_true(result.reflection_decision.has_value(),
              "reflection replay case should return a reflection decision");
  assert_true(result.reflection_decision->decision_kind.has_value() &&
                  *result.reflection_decision->decision_kind ==
                      dasall::contracts::ReflectionDecisionKind::Continue,
              "reflection replay case should stay on the Continue path");

  assert_trace_matches(output_dir,
                       "req-replay-reflect-continue",
                       "reflection",
                       "replay.trace.reflect.request");
  assert_trace_matches(output_dir,
                       "req-replay-reflect-continue",
                       "reflection",
                       "replay.trace.reflect.result");
}

void test_build_replay_trace_matches_golden_observation_projection() {
  MockCognitionFixture fixture(MockCognitionFixtureOptions{
      .request_id = "req-replay-build-projection",
      .trace_id = "trace-replay-build-projection",
      .profile_id = std::string{kReplayProfile},
      .observation_payload = R"({"dataset":"agent.dataset","status":"ok"})",
  });

  const auto output_dir = make_output_dir("build-projection");
  auto builder = create_response_builder(CognitionConfig{}, make_dependencies(output_dir));

  const auto result = builder->build(
      fixture.make_response_request(
          fixture.make_action_decision(ActionDecisionKind::DirectResponse),
          fixture.make_observation(true,
                                   R"({"dataset":"agent.dataset","status":"ok"})")));

  assert_true(result.agent_result.has_value(),
              "build replay case should materialize an agent result");
  assert_true(!result.fallback_used,
              "build replay case should keep the observation-projection path authoritative");
  assert_true(result.agent_result->status.has_value() &&
                  *result.agent_result->status == AgentResultStatus::Completed,
              "build replay case should finish with a completed agent status");

  assert_trace_matches(output_dir,
                       "req-replay-build-projection",
                       "response",
                       "replay.trace.build.request");
  assert_trace_matches(output_dir,
                       "req-replay-build-projection",
                       "response",
                       "replay.trace.build.result");
}

}  // namespace

int main() {
  try {
    test_decide_replay_trace_matches_golden_direct_response();
    test_decide_replay_trace_matches_golden_planning_fallback();
    test_reflect_replay_trace_matches_golden_continue_path();
    test_build_replay_trace_matches_golden_observation_projection();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
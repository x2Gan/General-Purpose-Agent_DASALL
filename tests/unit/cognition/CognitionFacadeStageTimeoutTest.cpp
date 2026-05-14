#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ICognitionEngine.h"
#include "decision/ActionDecision.h"
#include "error/ResultCode.h"
#include "../../../profiles/include/RuntimePolicySnapshot.h"
#include "support/TestAssertions.h"
#include "../../mocks/include/MockCognitionFixture.h"

namespace {

using dasall::cognition::CognitionRuntimeDependencies;
using dasall::cognition::CognitionStepRequest;
using dasall::cognition::ResponseBuildHints;
using dasall::cognition::ResponseBuildRequest;
using dasall::cognition::StageExecutionHints;
using dasall::cognition::create_cognition_engine;
using dasall::contracts::ResultCode;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected) {
  for (const auto& value : values) {
    if (value == expected) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool contains_stage(
    const std::vector<dasall::llm::LLMGenerateRequest>& requests,
    const std::string& expected_stage) {
  for (const auto& request : requests) {
    if (request.stage == expected_stage) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool contains_text(const std::string& text, const std::string& expected) {
  return text.find(expected) != std::string::npos;
}

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot(
    std::uint32_t llm_timeout_ms) {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return RuntimePolicySnapshot{
      88U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{.max_tokens = 4096U,
                                       .max_turns = 12U,
                                       .max_tool_calls = 4U,
                                       .max_latency_ms = 2400U,
                                       .max_replan_count = 2U},
      ModelProfile{.stage_routes = {
                       {"planning",
                        ModelRoutePolicy{.route = "llm.plan.primary",
                                         .fallback_route = "llm.plan.fallback",
                                         .streaming_enabled = false}},
                       {"execution",
                        ModelRoutePolicy{.route = "llm.exec.primary",
                                         .fallback_route = "llm.exec.fallback",
                                         .streaming_enabled = false}},
                       {"reflection",
                        ModelRoutePolicy{.route = "llm.reflect.primary",
                                         .fallback_route = "llm.reflect.fallback",
                                         .streaming_enabled = false}},
                       {"response",
                        ModelRoutePolicy{.route = "llm.response.primary",
                                         .fallback_route = "llm.response.fallback",
                                         .streaming_enabled = false}},
                   }},
      TokenBudgetPolicy{.max_input_tokens = 2048U,
                        .max_output_tokens = 768U,
                        .max_history_turns = 8U,
                        .compression_threshold = 1536U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 30000,
                            .expire_after_ms = 120000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 1000},
      DegradePolicy{.fallback_chain = {"template"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = llm_timeout_ms,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .tool = TimeoutBudget{.timeout_ms = 600,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 2U},
                    .mcp = TimeoutBudget{.timeout_ms = 600,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 2U},
                    .workflow = TimeoutBudget{.timeout_ms = 1500,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 2U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "core",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = false,
                .upgrade_strategy = "rolling"},
      4U,
  };
}

void test_decide_returns_stage_timeout_and_discards_late_planning_bridge_results() {
  constexpr auto kDeadlineMs = 40U;
  const auto slow_stage_delay = std::chrono::milliseconds(140);

  MockCognitionFixture fixture;
  std::atomic<int> planning_calls{0};
  fixture.llm_manager()->set_generate_handler(
      [&planning_calls, slow_stage_delay](const dasall::llm::LLMGenerateRequest& request) {
        if (request.stage == "planning" && planning_calls.fetch_add(1) == 0) {
          std::this_thread::sleep_for(slow_stage_delay);
        }

        return MockLLMManager::make_success_result(
            std::string{"mock-content-for-"} + request.stage,
            std::string{"mock.route."} + request.stage,
            request.request.request_id);
      });

  auto engine = create_cognition_engine(
      make_runtime_policy_snapshot(kDeadlineMs),
      CognitionRuntimeDependencies{
          .llm_manager = fixture.llm_manager(),
          .policy_snapshot = nullptr,
      });
    assert_true(engine != nullptr,
          "timeout decide test requires a snapshot that projects to a concrete cognition engine");
  auto request = fixture.make_decide_request(true);

  const auto timed_out_result = engine->decide(request);

  assert_true(timed_out_result.result_code.has_value(),
              "planning bridge timeout should emit a result code");
  assert_equal(static_cast<int>(ResultCode::RuntimeRetryExhausted),
               static_cast<int>(*timed_out_result.result_code),
               "stage timeout should stay in the frozen runtime failure code range");
  assert_true(timed_out_result.error_info.has_value(),
              "planning bridge timeout should return structured error info");
  assert_equal(std::string("planning"),
               timed_out_result.error_info->details.stage,
               "stage timeout should report the timed out canonical stage");
  assert_true(contains_text(timed_out_result.error_info->details.message,
                            "cognition.stage_timeout"),
              "timeout message should carry the cognition.stage_timeout marker");
  assert_true(contains_text(timed_out_result.error_info->details.message,
                            std::string{"request_id="} + request.request_id),
              "timeout message should include the request_id");
  assert_true(contains_text(timed_out_result.error_info->details.message,
                            std::string{"trace_id="} + request.trace_id),
              "timeout message should include the trace_id");
  assert_true(!timed_out_result.action_decision.has_value(),
              "planning bridge timeout should fail-fast before action synthesis");
  assert_true(contains_value(timed_out_result.diagnostics,
                             "decision_pipeline.stage_timeout:planning"),
              "timeout diagnostics should expose the failed planning stage");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "planning"),
              "planning timeout should still record the planning bridge request");
  assert_true(!contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "planning timeout should fail-fast before execution bridge dispatch");

  std::this_thread::sleep_for(slow_stage_delay + std::chrono::milliseconds(30));
  fixture.llm_manager()->clear_recorded_requests();

  const auto recovered_result = engine->decide(request);

  assert_true(!recovered_result.result_code.has_value(),
              "late planning result must not poison the next decide request");
  assert_true(recovered_result.action_decision.has_value(),
              "next decide request should recover once the timeouting call is gone");
  assert_true(contains_value(recovered_result.diagnostics, "decision_pipeline.completed"),
              "successful retry should complete the decision pipeline cleanly");
  assert_true(contains_stage(fixture.llm_manager()->generate_requests(), "planning") &&
                  contains_stage(fixture.llm_manager()->generate_requests(), "execution"),
              "successful retry should re-run planning and execution bridge stages");
}

void test_reflect_returns_stage_timeout_when_reflection_bridge_exceeds_deadline() {
  constexpr auto kDeadlineMs = 35U;
  const auto slow_stage_delay = std::chrono::milliseconds(120);

  MockCognitionFixture fixture;
  fixture.llm_manager()->set_generate_handler(
      [slow_stage_delay](const dasall::llm::LLMGenerateRequest& request) {
        if (request.stage == "reflection") {
          std::this_thread::sleep_for(slow_stage_delay);
        }

        return MockLLMManager::make_success_result(
            std::string{"mock-content-for-"} + request.stage,
            std::string{"mock.route."} + request.stage,
            request.request.request_id);
      });

  auto engine = create_cognition_engine(
      make_runtime_policy_snapshot(kDeadlineMs),
      CognitionRuntimeDependencies{
          .llm_manager = fixture.llm_manager(),
          .policy_snapshot = nullptr,
      });
    assert_true(engine != nullptr,
          "timeout reflection test requires a snapshot that projects to a concrete cognition engine");
  auto request = fixture.make_reflection_request(fixture.make_observation(true));

  const auto result = engine->reflect(request);

  assert_true(result.result_code.has_value(),
              "reflection bridge timeout should emit a result code");
  assert_equal(static_cast<int>(ResultCode::RuntimeRetryExhausted),
               static_cast<int>(*result.result_code),
               "reflection timeout should stay in the frozen runtime failure code range");
  assert_true(result.error_info.has_value(),
              "reflection bridge timeout should return structured error info");
  assert_equal(std::string("reflection"),
               result.error_info->details.stage,
               "reflection timeout should report the reflection canonical stage");
  assert_true(contains_text(result.error_info->details.message, "cognition.stage_timeout"),
              "reflection timeout message should carry the cognition.stage_timeout marker");
  assert_true(!result.reflection_decision.has_value(),
              "reflection bridge timeout should fail-fast before reflection analysis returns");
  assert_true(contains_value(result.diagnostics,
                             "reflection_pipeline.stage_timeout:reflection"),
              "reflection timeout diagnostics should expose the timed out stage");
}

}  // namespace

int main() {
  try {
    test_decide_returns_stage_timeout_and_discards_late_planning_bridge_results();
    test_reflect_returns_stage_timeout_when_reflection_bridge_exceeds_deadline();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
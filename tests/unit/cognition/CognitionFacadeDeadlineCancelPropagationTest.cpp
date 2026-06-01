#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

#include "ICognitionEngine.h"
#include "error/ResultCode.h"
#include "support/TestAssertions.h"

#include "../../mocks/include/MockCognitionFixture.h"

#include "../../../profiles/include/RuntimePolicySnapshot.h"

namespace {

using dasall::cognition::CognitionRuntimeDependencies;
using dasall::cognition::create_cognition_engine;
using dasall::contracts::ResultCode;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockLLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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

[[nodiscard]] bool wait_for_equal(const std::atomic<int>& value,
                                  int expected,
                                  std::chrono::milliseconds timeout) {
  const auto started_at = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - started_at < timeout) {
    if (value.load() == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return value.load() == expected;
}

void test_deadline_timeout_invokes_abandon_call_without_waiting_for_completion() {
  constexpr auto kDeadlineMs = 40U;
  const auto slow_generate_delay = std::chrono::milliseconds(220);
  const auto slow_abandon_delay = std::chrono::milliseconds(180);
  const auto max_timeout_return = std::chrono::milliseconds(120);

  MockCognitionFixture fixture;
  std::atomic<int> abandon_calls{0};
  fixture.llm_manager()->set_generate_handler(
      [slow_generate_delay](const dasall::llm::LLMGenerateRequest& request) {
        if (request.stage == "planning") {
          std::this_thread::sleep_for(slow_generate_delay);
        }

        return MockLLMManager::make_success_result(
            std::string{"mock-content-for-"} + request.stage,
            std::string{"mock.route."} + request.stage,
            request.request.request_id);
      });
  fixture.llm_manager()->set_abandon_handler(
      [&abandon_calls, slow_abandon_delay](std::string_view) {
        abandon_calls.fetch_add(1);
        std::this_thread::sleep_for(slow_abandon_delay);
        return true;
      });

  auto engine = create_cognition_engine(
      make_runtime_policy_snapshot(kDeadlineMs),
      CognitionRuntimeDependencies{
          .llm_manager = fixture.llm_manager(),
          .policy_snapshot = nullptr,
      });
  assert_true(engine != nullptr,
              "deadline cancel propagation test requires a concrete cognition engine");

  const auto request = fixture.make_decide_request(true);
  const auto started_at = std::chrono::steady_clock::now();
  const auto result = engine->decide(request);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started_at);

  assert_true(result.result_code.has_value(),
              "planning timeout should still surface a result code");
  assert_equal(static_cast<int>(ResultCode::RuntimeRetryExhausted),
               static_cast<int>(*result.result_code),
               "planning timeout should keep the runtime retry exhausted result code");
  assert_true(elapsed < max_timeout_return,
              "deadline timeout should not wait for abandon_call completion before returning");
  assert_true(wait_for_equal(abandon_calls, 1, std::chrono::milliseconds(120)),
              "deadline timeout should invoke abandon_call exactly once");
  assert_equal(1,
               fixture.llm_manager()->abandon_call_count(),
               "deadline timeout should only issue one abandon_call request");
  assert_true(fixture.llm_manager()->last_abandoned_call_id().has_value(),
              "deadline timeout should capture the abandoned llm_call_id");
  assert_equal(request.request_id + ":planning:plan",
               *fixture.llm_manager()->last_abandoned_call_id(),
               "deadline timeout should abandon the canonical planning llm_call_id");

  std::this_thread::sleep_for(slow_generate_delay + slow_abandon_delay +
                              std::chrono::milliseconds(40));
}

}  // namespace

int main() {
  try {
    test_deadline_timeout_invokes_abandon_call_without_waiting_for_completion();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
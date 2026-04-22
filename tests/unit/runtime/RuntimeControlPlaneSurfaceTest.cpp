#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "AgentFacade.h"
#include "IAgent.h"
#include "RuntimePolicySnapshot.h"
#include "agent/AgentRequest.h"
#include "agent/AgentResult.h"
#include "checkpoint/RuntimeBudget.h"
#include "support/TestAssertions.h"

namespace dasall::runtime {

class RuntimeDependencySet {};

}  // namespace dasall::runtime

namespace {

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> make_policy_snapshot() {
  using dasall::contracts::RuntimeBudget;
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

  ModelProfile model_profile;
  model_profile.stage_routes.emplace("main", ModelRoutePolicy{
                                                 .route = "mock-primary",
                                                 .fallback_route = std::string{"mock-fallback"},
                                                 .streaming_enabled = false,
                                             });

  return std::make_shared<RuntimePolicySnapshot>(
      1U,
      "desktop_full",
      RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = 2U,
          .max_latency_ms = 2000U,
          .max_replan_count = 2U,
      },
      model_profile,
      TokenBudgetPolicy{
          .max_input_tokens = 2048U,
          .max_output_tokens = 512U,
          .max_history_turns = 8U,
          .compression_threshold = 1024U,
      },
      PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"runtime"},
          .tool_visibility_rules = {"tools:allow"},
      },
      CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 5000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 100,
      },
      DegradePolicy{
          .fallback_chain = {"safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .tool = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .mcp = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .workflow = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"diagnostic"},
      },
      OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "detailed",
          .trace_sample_ratio = 1.0,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      2U);
}

}  // namespace

int main() {
  using dasall::contracts::AgentRequest;
  using dasall::contracts::AgentResultStatus;
  using dasall::contracts::RequestChannel;
  using dasall::runtime::AgentFacade;
  using dasall::runtime::AgentInitRequest;
  using dasall::runtime::IAgent;
  using dasall::runtime::ResumeHandleRequest;
  using dasall::runtime::RuntimeDependencySet;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  try {
    std::unique_ptr<IAgent> agent = std::make_unique<AgentFacade>();

    AgentInitRequest init_request;
    init_request.runtime_instance_id = "rt-025";
    init_request.profile_id = "desktop_full";
    init_request.policy_snapshot = make_policy_snapshot();
    init_request.dependency_set = std::make_shared<RuntimeDependencySet>();
    init_request.boot_reason = "surface-test";

    const auto init_result = agent->init(init_request);
    assert_true(init_result.is_ready(), "runtime facade should accept the minimum valid init request");
    assert_equal("rt-025", init_result.runtime_instance_id, "runtime instance id should round-trip through init result");
    assert_equal("desktop_full", init_result.resolved_profile_id, "profile id should round-trip through init result");

    AgentRequest handle_request;
    handle_request.request_id = std::string{"req-025"};
    handle_request.session_id = std::string{"session-025"};
    handle_request.trace_id = std::string{"trace-025"};
    handle_request.user_input = std::string{"ping"};
    handle_request.request_channel = RequestChannel::Cli;
    handle_request.created_at = 42;

    const auto handle_result = agent->handle(handle_request);
    assert_true(handle_result.status.has_value() &&
                    *handle_result.status == AgentResultStatus::Failed,
                "unwired facade handle path should fail closed");
    assert_true(handle_result.request_id.has_value() && *handle_result.request_id == "req-025",
                "handle result should preserve request correlation");
    assert_true(handle_result.trace_id.has_value() && *handle_result.trace_id == "trace-025",
                "handle result should preserve trace correlation");
    assert_true(handle_result.response_text.has_value() &&
                    handle_result.response_text->find("control-plane skeleton is initialized") !=
                        std::string::npos,
                "handle result should explain the current fail-closed surface state");

    ResumeHandleRequest resume_request;
    resume_request.request_id = "resume-025";
    resume_request.session_id = "session-025";
    resume_request.trace_context = "trace-025";

    const auto resume_result = agent->resume(resume_request);
    assert_true(resume_result.status.has_value() &&
                    *resume_result.status == AgentResultStatus::Failed,
                "resume should also fail closed when checkpoint anchors are missing");
    assert_true(resume_result.response_text.has_value() &&
                    resume_result.response_text->find("missing required checkpoint anchors") !=
                        std::string::npos,
                "resume should reject incomplete checkpoint anchors explicitly");

    assert_true(agent->stop(100U), "runtime facade stop should succeed for a live facade instance");
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#pragma once

#include <memory>
#include <string>

#include "AgentTypes.h"
#include "RuntimePolicySnapshot.h"
#include "agent/AgentRequest.h"
#include "checkpoint/RuntimeBudget.h"

namespace dasall::runtime {

class RuntimeDependencySet {};

}  // namespace dasall::runtime

namespace dasall::tests::runtime_fixture {

inline std::shared_ptr<const profiles::RuntimePolicySnapshot> make_policy_snapshot(
    std::string profile_id = "desktop_full") {
  profiles::ModelProfile model_profile;
  model_profile.stage_routes.emplace("main", profiles::ModelRoutePolicy{
                                                 .route = "mock-primary",
                                                 .fallback_route = std::string{"mock-fallback"},
                                                 .streaming_enabled = false,
                                             });

  return std::make_shared<profiles::RuntimePolicySnapshot>(
      1U,
      std::move(profile_id),
      contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = 2U,
          .max_latency_ms = 2000U,
          .max_replan_count = 2U,
      },
      std::move(model_profile),
      profiles::TokenBudgetPolicy{
          .max_input_tokens = 2048U,
          .max_output_tokens = 512U,
          .max_history_turns = 8U,
          .compression_threshold = 1024U,
      },
      profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"runtime"},
          .tool_visibility_rules = {"tools:allow"},
      },
      profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 5000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 100,
      },
      profiles::DegradePolicy{
          .fallback_chain = {"safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      profiles::TimeoutPolicy{
          .llm = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .tool = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .mcp = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .workflow = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"diagnostic"},
      },
      profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "detailed",
          .trace_sample_ratio = 1.0,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      2U);
}

inline runtime::AgentInitRequest make_init_request(
    std::string runtime_instance_id = "rt-unary-fixture",
    std::string profile_id = "desktop_full",
    std::string boot_reason = "runtime-unary-fixture") {
  runtime::AgentInitRequest request;
  request.runtime_instance_id = std::move(runtime_instance_id);
  request.profile_id = profile_id;
  request.policy_snapshot = make_policy_snapshot(std::move(profile_id));
  request.dependency_set = std::make_shared<runtime::RuntimeDependencySet>();
  request.boot_reason = std::move(boot_reason);
  request.cold_start = true;
  return request;
}

inline contracts::AgentRequest make_agent_request(
    std::string request_id = "req-unary-fixture",
    std::string session_id = "session-unary-fixture",
    std::string trace_id = "trace-unary-fixture",
    std::string user_input = "summarize runtime status") {
  contracts::AgentRequest request;
  request.request_id = std::move(request_id);
  request.session_id = std::move(session_id);
  request.trace_id = std::move(trace_id);
  request.user_input = std::move(user_input);
  request.request_channel = contracts::RequestChannel::Cli;
  request.created_at = 1710000000000;
  return request;
}

inline runtime::ResumeHandleRequest make_incomplete_resume_request(
    std::string request_id = "resume-unary-fixture",
    std::string session_id = "session-unary-fixture",
    std::string trace_context = "trace-unary-fixture") {
  runtime::ResumeHandleRequest request;
  request.request_id = std::move(request_id);
  request.session_id = std::move(session_id);
  request.trace_context = std::move(trace_context);
  return request;
}

}  // namespace dasall::tests::runtime_fixture
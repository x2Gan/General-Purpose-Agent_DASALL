#pragma once

#include <optional>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "ToolInvocationContext.h"
#include "tool/ToolDescriptor.h"
#include "tool/ToolRequest.h"

namespace dasall::tests::support {

struct ToolsSnapshotOverrides {
  std::string profile_id = "desktop_full";
  std::vector<std::string> allowed_tool_domains = {"builtin"};
  std::vector<std::string> tool_visibility_rules = {"builtin:all"};
  bool safe_mode_enabled = true;
  bool requires_high_risk_confirmation = true;
  bool stale_read_allowed = false;
  std::uint32_t tool_timeout_ms = 2500;
  std::uint32_t mcp_timeout_ms = 2000;
  std::uint32_t workflow_timeout_ms = 5000;
};

[[nodiscard]] inline profiles::RuntimePolicySnapshot make_integration_snapshot(
    const ToolsSnapshotOverrides& overrides = {}) {
  return profiles::RuntimePolicySnapshot{
      1U,
      overrides.profile_id,
      contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
      },
      profiles::ModelProfile{
          .stage_routes = {{
              "planner",
              profiles::ModelRoutePolicy{
                  .route = "local.small",
                  .fallback_route = std::string("builtin_only"),
                  .streaming_enabled = false,
              },
          }},
      },
      profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
      },
      profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = overrides.tool_visibility_rules,
      },
      profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = overrides.stale_read_allowed,
          .failure_backoff_ms = 5000,
      },
      profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
      },
      profiles::TimeoutPolicy{
          .llm = profiles::TimeoutBudget{
              .timeout_ms = 1800,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
          .tool = profiles::TimeoutBudget{
              .timeout_ms = overrides.tool_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 4U,
          },
          .mcp = profiles::TimeoutBudget{
              .timeout_ms = overrides.mcp_timeout_ms,
              .retry_budget = 1U,
              .circuit_breaker_threshold = 3U,
          },
          .workflow = profiles::TimeoutBudget{
              .timeout_ms = overrides.workflow_timeout_ms,
              .retry_budget = 0U,
              .circuit_breaker_threshold = 3U,
          },
      },
      profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = overrides.requires_high_risk_confirmation,
          .safe_mode_enabled = overrides.safe_mode_enabled,
          .audit_level = "full",
          .allowed_tool_domains = overrides.allowed_tool_domains,
      },
      profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      4U};
}

[[nodiscard]] inline tools::ToolInvocationContext make_integration_context(
    const profiles::RuntimePolicySnapshot* snapshot,
    const std::string& session_suffix = "default") {
  return tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-integration-") + session_suffix,
      .profile_snapshot = snapshot,
      .trace = {
          .trace_id = std::string("trace-integration-") + session_suffix,
          .span_id = std::string("span-integration-") + session_suffix,
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<tools::ToolConfirmationFact>{
          tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-integration-") + session_suffix,
              .subject_ref = std::string("goal://integration-") + session_suffix,
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 900,
          }},
  };
}

[[nodiscard]] inline contracts::ToolDescriptor make_builtin_action_descriptor(
    const std::string& tool_name = "agent.terminal") {
  return contracts::ToolDescriptor{
      .tool_name = tool_name,
      .display_name = std::string("Agent Terminal"),
      .category = contracts::ToolCategory::Action,
      .capability_tier = contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = true,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/") + tool_name + "/input/v1",
      .output_schema_ref = std::string("schema://tools/") + tool_name + "/output/v1",
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "action"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] inline contracts::ToolDescriptor make_builtin_query_descriptor(
    const std::string& tool_name = "agent.dataset") {
  return contracts::ToolDescriptor{
      .tool_name = tool_name,
      .display_name = std::string("Agent Dataset"),
      .category = contracts::ToolCategory::Information,
      .capability_tier = contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/") + tool_name + "/input/v1",
      .output_schema_ref = std::string("schema://tools/") + tool_name + "/output/v1",
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"builtin", "query"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] inline contracts::ToolRequest make_action_request(
    const std::string& tool_name = "agent.terminal",
    const std::string& suffix = "default") {
  return contracts::ToolRequest{
      .request_id = std::string("req-") + suffix,
      .tool_call_id = std::string("call-") + suffix,
      .tool_name = tool_name,
      .invocation_kind = contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo test\"}"),
      .created_at = 1000,
      .goal_id = std::string("goal-") + suffix,
      .worker_task_id = std::string("worker-") + suffix,
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-") + suffix,
      .tags = std::vector<std::string>{"integration", "tools"},
  };
}

[[nodiscard]] inline contracts::ToolRequest make_query_request(
    const std::string& tool_name = "agent.dataset",
    const std::string& suffix = "default") {
  return contracts::ToolRequest{
      .request_id = std::string("req-") + suffix,
      .tool_call_id = std::string("call-") + suffix,
      .tool_name = tool_name,
      .invocation_kind = contracts::ToolInvocationKind::InformationQuery,
      .arguments_payload = std::string("{\"scope\":\"session\"}"),
      .created_at = 1001,
      .goal_id = std::string("goal-") + suffix,
      .worker_task_id = std::string("worker-") + suffix,
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-") + suffix,
      .tags = std::vector<std::string>{"integration", "tools", "query"},
  };
}

}  // namespace dasall::tests::support

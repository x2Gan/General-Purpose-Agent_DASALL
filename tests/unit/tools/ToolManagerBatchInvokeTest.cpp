#include <exception>
#include <iostream>
#include <memory>
#include <vector>

#include "ToolManager.h"
#include "RuntimePolicySnapshot.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::contracts::ToolRequest make_request(
    int request_index,
    std::string request_id,
    std::string tool_call_id,
    std::string tool_name,
    dasall::contracts::ToolInvocationKind invocation_kind) {
  return dasall::contracts::ToolRequest{
      .request_id = std::move(request_id),
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::move(tool_name),
      .invocation_kind = invocation_kind,
      .arguments_payload = std::string("{\"index\":") + std::to_string(request_index) + "}",
      .created_at = 1000 + request_index,
      .goal_id = std::string("goal-batch"),
      .worker_task_id = std::string("worker-batch"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-") + std::to_string(request_index),
      .tags = std::vector<std::string>{"builtin"},
  };
}

    [[nodiscard]] dasall::contracts::ToolDescriptor make_descriptor(
      std::string tool_name,
      dasall::contracts::ToolCategory category,
      bool is_read_only) {
      return dasall::contracts::ToolDescriptor{
        .tool_name = std::move(tool_name),
        .display_name = std::string("Batch Tool"),
        .category = category,
        .capability_tier = dasall::contracts::ToolCapabilityTier::Stable,
        .is_read_only = is_read_only,
        .supports_compensation = false,
        .default_timeout_ms = 2500U,
        .input_schema_ref = std::string("schema://tools/batch/input"),
        .output_schema_ref = std::string("schema://tools/batch/output"),
        .required_scopes = std::vector<std::string>{"tools.execute"},
        .tags = std::vector<std::string>{"builtin"},
        .version = std::string("1.0.0"),
      };
    }

    [[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
      return dasall::profiles::RuntimePolicySnapshot{
        1U,
        "desktop_full",
        dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 8U,
          .max_tool_calls = 24U,
          .max_latency_ms = 8000U,
          .max_replan_count = 2U,
        },
        dasall::profiles::ModelProfile{
          .stage_routes = {{
            "planner",
            dasall::profiles::ModelRoutePolicy{
              .route = "local.small",
              .fallback_route = std::string("builtin_only"),
              .streaming_enabled = false,
            },
          }},
        },
        dasall::profiles::TokenBudgetPolicy{
          .max_input_tokens = 1024U,
          .max_output_tokens = 512U,
          .max_history_turns = 4U,
          .compression_threshold = 768U,
        },
        dasall::profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"builtin:all"},
        },
        dasall::profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 10000,
          .expire_after_ms = 180000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 5000,
        },
        dasall::profiles::DegradePolicy{
          .fallback_chain = {"builtin_only"},
          .allow_model_failover = false,
          .allow_budget_degrade = true,
        },
        dasall::profiles::TimeoutPolicy{
          .llm = dasall::profiles::TimeoutBudget{
            .timeout_ms = 1800,
            .retry_budget = 0U,
            .circuit_breaker_threshold = 3U,
          },
          .tool = dasall::profiles::TimeoutBudget{
            .timeout_ms = 2500,
            .retry_budget = 1U,
            .circuit_breaker_threshold = 4U,
          },
          .mcp = dasall::profiles::TimeoutBudget{
            .timeout_ms = 2000,
            .retry_budget = 1U,
            .circuit_breaker_threshold = 3U,
          },
          .workflow = dasall::profiles::TimeoutBudget{
            .timeout_ms = 5000,
            .retry_budget = 1U,
            .circuit_breaker_threshold = 3U,
          },
        },
        dasall::profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
        },
        dasall::profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "full",
          .trace_sample_ratio = 0.5,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
        },
        4U};
    }

    void test_invoke_batch_preserves_request_level_isolation_and_keeps_following_requests_running() {
      int execution_count = 0;
      dasall::tools::manager::ToolManagerDependencies dependencies;
      dependencies.registry = std::make_shared<dasall::tools::registry::ToolRegistry>(
        std::vector<dasall::contracts::ToolDescriptor>{
          make_descriptor("tool.apply", dasall::contracts::ToolCategory::Action, false),
          make_descriptor("tool.inspect", dasall::contracts::ToolCategory::Information, true),
        });
      dependencies.executor = [&execution_count](
                    const dasall::tools::manager::ToolExecutionRequest& execution_request) {
      ++execution_count;
      return dasall::contracts::ToolResult{
        .request_id = execution_request.tool_ir.request_id,
        .tool_call_id = execution_request.tool_ir.tool_call_id,
        .tool_name = execution_request.tool_ir.tool_name,
        .success = true,
        .payload = std::string("{\"status\":\"ok\"}"),
        .error = std::nullopt,
        .side_effects = std::nullopt,
        .completed_at = 2000,
        .duration_ms = 4,
        .goal_id = execution_request.tool_ir.goal_id,
        .worker_task_id = execution_request.tool_ir.worker_task_id,
        .tags = std::vector<std::string>{"batch"},
      };
      };

      dasall::tools::ToolManager manager(std::move(dependencies));
      const auto snapshot = make_snapshot();
      const dasall::tools::ToolInvocationContext context{
        .caller_domain = std::string("runtime.main"),
        .session_id = std::string("session-batch"),
        .profile_snapshot = &snapshot,
        .trace = {
          .trace_id = std::string("trace-batch"),
          .span_id = std::string("span-batch"),
          .parent_span_id = std::nullopt,
        },
        .confirmation_facts = std::nullopt,
      };
  const std::vector<dasall::contracts::ToolRequest> requests{
        make_request(1, "req-1", "call-1", "tool.apply",
               dasall::contracts::ToolInvocationKind::Action),
        make_request(2, "req-2", "call-2", "tool.inspect",
               dasall::contracts::ToolInvocationKind::InformationQuery),
  };

      const auto envelopes = manager.invoke_batch(requests, context);
  assert_equal(static_cast<int>(requests.size()), static_cast<int>(envelopes.size()),
               "invoke_batch should return one envelope per request");

      assert_true(envelopes[0].tool_result.has_value(),
            "batch invoke should still return a tool_result for denied items");
      assert_equal(*requests[0].request_id, *envelopes[0].tool_result->request_id,
             "denied batch item should preserve request identity");
      assert_equal(std::string("policy.confirmation_required"),
             *envelopes[0].failure_reason_code,
             "high-risk item without confirmation should fail closed in batch mode");
      assert_true(!envelopes[0].has_projection(),
            "denied batch item should not fabricate an observation projection");

      assert_true(envelopes[1].tool_result.has_value(),
            "following batch item should still execute after a prior denial");
      assert_equal(*requests[1].request_id, *envelopes[1].tool_result->request_id,
             "successful batch item should preserve request identity");
      assert_true(envelopes[1].tool_result->success.value_or(false),
            "read-only information request should still succeed in the same batch");
      assert_true(envelopes[1].has_projection(),
            "successful batch item should include observation and digest projection");
      assert_true(!envelopes[1].failure_reason_code.has_value(),
            "successful batch item should not retain a failure reason");
      assert_equal(1, execution_count,
             "only the admitted request should reach the executor in batch mode");
}

}  // namespace

int main() {
  try {
    test_invoke_batch_preserves_request_level_isolation_and_keeps_following_requests_running();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
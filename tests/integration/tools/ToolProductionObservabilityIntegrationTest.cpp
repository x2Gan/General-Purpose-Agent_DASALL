#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ObservabilityLiveComposition.h"
#include "RuntimePolicySnapshot.h"
#include "ServiceLiveComposition.h"
#include "ToolManager.h"
#include "audit/AuditService.h"
#include "bridge/ToolServiceBridge.h"
#include "execution/BuiltinExecutorLane.h"
#include "health/IHealthMonitor.h"
#include "metrics/MetricsFacade.h"
#include "ops/ToolAuditBridge.h"
#include "ops/ToolHealthProbe.h"
#include "ops/ToolMetricsBridge.h"
#include "ops/ToolTraceBridge.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"
#include "tracing/TracerProviderImpl.h"

namespace {

using dasall::tests::support::assert_true;

class ToolObservabilitySignalProvider final
    : public dasall::tools::ops::IToolHealthSignalProvider {
 public:
  ToolObservabilitySignalProvider(
      std::shared_ptr<dasall::tools::ops::ToolAuditBridge> audit_bridge,
      std::shared_ptr<dasall::tools::ops::ToolMetricsBridge> metrics_bridge,
      std::shared_ptr<dasall::tools::ops::ToolTraceBridge> trace_bridge)
      : audit_bridge_(std::move(audit_bridge)),
        metrics_bridge_(std::move(metrics_bridge)),
        trace_bridge_(std::move(trace_bridge)) {}

  [[nodiscard]] dasall::tools::ops::ToolHealthSample sample(std::int64_t) override {
    dasall::tools::ops::ToolHealthSample sample;
    sample.registry.revision = 1U;
    sample.registry.descriptor_catalog_ready = true;
    sample.builtin_lane.available = true;
    sample.builtin_lane.concurrency_budget = 1U;
    sample.workflow_lane.available = true;
    sample.workflow_lane.concurrency_budget = 1U;
    sample.mcp.session_ready = true;
    sample.mcp.freshness = dasall::tools::CapabilityFreshness::fresh;
    sample.mcp.stale_read_allowed = true;
    sample.audit_bridge_degraded = audit_bridge_ == nullptr ||
        audit_bridge_->get_status().degraded;
    sample.metrics_bridge_degraded = metrics_bridge_ == nullptr ||
        metrics_bridge_->is_degraded();
    sample.trace_bridge_degraded = trace_bridge_ == nullptr ||
        trace_bridge_->is_degraded();
    sample.sampled_at_unix_ms = 1712800000000;
    sample.detail_ref = "status://tools/health/production-observability";
    return sample;
  }

 private:
  std::shared_ptr<dasall::tools::ops::ToolAuditBridge> audit_bridge_;
  std::shared_ptr<dasall::tools::ops::ToolMetricsBridge> metrics_bridge_;
  std::shared_ptr<dasall::tools::ops::ToolTraceBridge> trace_bridge_;
};

[[nodiscard]] bool has_audit_action(const dasall::infra::ExportResult& result,
                                    const std::string& expected_action) {
  return std::any_of(result.records.begin(), result.records.end(),
                     [&expected_action](const dasall::infra::AuditEvent& event) {
                       return event.action == expected_action;
                     });
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

[[nodiscard]] dasall::contracts::ToolDescriptor make_action_descriptor() {
  return dasall::contracts::ToolDescriptor{
      .tool_name = std::string("agent.terminal"),
      .display_name = std::string("Agent Terminal"),
      .category = dasall::contracts::ToolCategory::Action,
      .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
      .is_read_only = false,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string("schema://tools/agent.terminal/input/v1"),
      .output_schema_ref = std::string("schema://tools/agent.terminal/output/v1"),
      .required_scopes = std::vector<std::string>{"tools.execute"},
      .tags = std::vector<std::string>{"builtin", "action"},
      .version = std::string("1.0.0"),
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_action_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-tool-observability-action"),
      .tool_call_id = std::string("call-tool-observability-action"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo observability\"}"),
      .created_at = 1712800000000,
      .goal_id = std::string("goal-tool-observability-action"),
      .worker_task_id = std::string("worker-tool-observability-action"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-tool-observability-action"),
      .tags = std::vector<std::string>{"integration", "tools", "action"},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context(
    const dasall::profiles::RuntimePolicySnapshot& snapshot) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-tool-observability"),
      .profile_snapshot = &snapshot,
      .trace = {
          .trace_id = std::string("trace-tool-observability"),
          .span_id = std::string("span-tool-observability"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-tool-observability"),
              .subject_ref = std::string("goal://tool-observability"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 1712799999000,
          }},
  };
}

void test_tool_production_observability_integration_uses_shared_sinks_and_health() {
  const auto snapshot = make_snapshot();
  const auto observability = dasall::infra::compose_live_observability(
      dasall::infra::ObservabilityLiveCompositionOptions{
          .profile_id = snapshot.effective_profile_id(),
          .metrics_granularity = snapshot.ops_policy().metrics_granularity,
          .trace_sample_ratio = snapshot.ops_policy().trace_sample_ratio,
      });
  assert_true(observability.ok(),
              std::string("tool production observability integration should compose shared sinks: ") +
                  observability.error);

  const auto audit_service =
      std::dynamic_pointer_cast<dasall::infra::audit::AuditService>(observability.audit_logger);
  const auto metrics_facade =
      std::dynamic_pointer_cast<dasall::infra::metrics::MetricsFacade>(observability.metrics_provider);
  const auto tracer_provider = std::dynamic_pointer_cast<
      dasall::infra::tracing::TracerProviderImpl>(observability.tracer_provider);
  assert_true(audit_service != nullptr && metrics_facade != nullptr && tracer_provider != nullptr,
              "tool production observability integration should keep concrete audit, metrics, and trace providers inspectable in focused tests");

  const auto live_services = dasall::services::compose_live_services(
      snapshot,
      dasall::services::ServiceLiveCompositionOptions{
          .observability_enabled = true,
          .observability_level = snapshot.ops_policy().metrics_granularity,
          .audit_logger = observability.audit_logger,
          .metrics_provider = observability.metrics_provider,
          .tracer_provider = observability.tracer_provider,
          .health_probe_enabled = true,
      });
  assert_true(live_services.ok() && live_services.health_probe != nullptr,
              std::string("tool production observability integration should compose live services with a health probe: ") +
                  live_services.error);

  auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
  assert_true(registry->register_builtin(make_action_descriptor()),
              "tool production observability integration should register the builtin action descriptor");

  auto tool_audit_bridge = std::make_shared<dasall::tools::ops::ToolAuditBridge>(
      observability.audit_logger.get());
  auto tool_metrics_bridge = std::make_shared<dasall::tools::ops::ToolMetricsBridge>(
      observability.metrics_provider,
      dasall::tools::ops::ToolMetricsBridgeOptions{
          .enabled = true,
          .profile_id = snapshot.effective_profile_id(),
          .metrics_granularity = snapshot.ops_policy().metrics_granularity,
      });
  auto tool_trace_bridge = std::make_shared<dasall::tools::ops::ToolTraceBridge>(
      observability.tracer_provider,
      dasall::tools::ops::ToolTraceBridgeOptions{
          .enabled = true,
          .profile_id = snapshot.effective_profile_id(),
          .trace_sample_ratio = snapshot.ops_policy().trace_sample_ratio,
      });
  auto tool_health_probe = std::make_shared<dasall::tools::ops::ToolHealthProbe>(
      std::make_shared<ToolObservabilitySignalProvider>(
          tool_audit_bridge,
          tool_metrics_bridge,
          tool_trace_bridge));

  assert_true(observability.health_monitor->register_probe(
                  dasall::infra::HealthProbeRegistration{
                      .probe_name = std::string(dasall::tools::ops::kToolHealthProbeName),
                      .probe_group = std::string(dasall::tools::ops::kToolHealthProbeGroup),
                      .probe = tool_health_probe.get(),
                  })
                  .ok,
              "tool production observability integration should register the tool health probe");
  assert_true(observability.health_monitor->register_probe(
                  dasall::infra::HealthProbeRegistration{
                      .probe_name = std::string("services.capability"),
                      .probe_group = std::string("readiness"),
                      .probe = live_services.health_probe.get(),
                  })
                  .ok,
              "tool production observability integration should register the services health probe");

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = live_services.execution_service,
          .data_service = live_services.data_service,
          .now_ms = {},
      });

  dasall::tools::manager::ToolManagerDependencies dependencies;
  dependencies.registry = registry;
  dependencies.metrics_bridge = tool_metrics_bridge;
  dependencies.trace_bridge = tool_trace_bridge;
  dependencies.audit_hooks =
      dasall::tools::ops::ToolAuditBridge::bind_hooks(tool_audit_bridge);
  dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  dasall::tools::ToolManager manager(std::move(dependencies));

  const auto envelope = manager.invoke(make_action_request(), make_context(snapshot));
  assert_true(envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false),
              "tool production observability integration should keep the live action path successful");
  assert_true(envelope.tool_result->payload.has_value() &&
                  envelope.tool_result->payload->find("\"operation\":\"agent.terminal\"") != std::string::npos,
              "tool production observability integration should preserve the live execution payload");
  assert_true(audit_service->primary_record_count() >= 4U,
              "tool production observability integration should emit both tool and service audit events into the shared audit service");

  const auto audit_export = audit_service->export_audit(dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  });
  assert_true(has_audit_action(audit_export, "tool.execution.requested") &&
                  has_audit_action(audit_export, "service.execution.requested"),
              "tool production observability integration should route tool and service audit actions into the shared audit sink");
  assert_true(metrics_facade->record_attempt_count() > 0U,
              "tool production observability integration should emit metrics samples into the shared metrics facade");
  assert_true(tracer_provider->tracer_count() >= 2U,
              "tool production observability integration should create both tools and services tracer scopes");

  const auto health_result = observability.health_monitor->evaluate_now();
  assert_true(health_result.ok && health_result.snapshot.readiness &&
                  !health_result.snapshot.degraded &&
                  health_result.snapshot.failed_components.empty(),
              "tool production observability integration should keep the shared health monitor ready when tool and services probes are healthy");
}

}  // namespace

int main() {
  try {
    test_tool_production_observability_integration_uses_shared_sinks_and_health();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
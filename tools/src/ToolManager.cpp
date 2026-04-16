#include "ToolManager.h"

#include <chrono>
#include <utility>

#include "error/ResultCode.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::contracts::ResultCodeCategory;
using dasall::contracts::ToolRequest;
using dasall::contracts::ToolResult;
using dasall::tools::CompensationRequest;
using dasall::tools::ToolInvocationEnvelope;
using dasall::tools::manager::ToolAuditHooks;

[[nodiscard]] std::int64_t current_time_ms() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch())
			.count();
}

[[nodiscard]] ErrorInfo build_error(
		ResultCode result_code,
		std::string message,
		std::string stage,
		std::string ref_id) {
	return ErrorInfo{
			.failure_type = dasall::contracts::classify_result_code(result_code),
			.retryable = false,
			.safe_to_replan = true,
			.details = ErrorDetails{
					.code = static_cast<int>(result_code),
					.message = std::move(message),
					.stage = std::move(stage),
			},
			.source_ref = ErrorSourceRefMinimal{
					.ref_type = "tool_manager",
					.ref_id = std::move(ref_id),
			},
	};
}

[[nodiscard]] ToolResult build_failure_result(
		const ToolRequest& request,
		std::string reason_code,
		std::string stage) {
	return ToolResult{
			.request_id = request.request_id,
			.tool_call_id = request.tool_call_id,
			.tool_name = request.tool_name,
			.success = false,
			.payload = std::nullopt,
			.error = build_error(
					ResultCode::ToolExecutionFailed,
					reason_code,
					std::move(stage),
					request.tool_name.value_or(std::string("unknown_tool"))),
			.side_effects = std::nullopt,
			.completed_at = current_time_ms(),
			.duration_ms = 0,
			.goal_id = request.goal_id,
			.worker_task_id = request.worker_task_id,
			.tags = request.tags,
	};
}

[[nodiscard]] ToolInvocationEnvelope build_failure_envelope(
		const ToolRequest& request,
		std::string reason_code,
		std::string stage) {
	ToolInvocationEnvelope envelope;
	envelope.tool_result = build_failure_result(request, reason_code, std::move(stage));
	envelope.failure_reason_code = std::move(reason_code);
	return envelope;
}

[[nodiscard]] ToolInvocationEnvelope build_compensation_failure_envelope(
		const CompensationRequest& request,
		std::string reason_code,
		std::string stage) {
	ToolInvocationEnvelope envelope;
	envelope.tool_result = ToolResult{
			.request_id = std::nullopt,
			.tool_call_id = request.tool_call_id,
			.tool_name = std::nullopt,
			.success = false,
			.payload = std::nullopt,
			.error = build_error(
					ResultCode::ToolExecutionFailed,
					reason_code,
					std::move(stage),
					request.tool_call_id.value_or(std::string("unknown_call"))),
			.side_effects = std::nullopt,
			.completed_at = current_time_ms(),
			.duration_ms = 0,
			.goal_id = std::nullopt,
			.worker_task_id = std::nullopt,
			.tags = std::nullopt,
	};
	envelope.failure_reason_code = std::move(reason_code);
	return envelope;
}

[[nodiscard]] bool is_successful(const ToolInvocationEnvelope& envelope) {
	return envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false);
}

void emit_terminal_audit(
		const ToolAuditHooks& hooks,
		const ToolInvocationEnvelope& envelope) {
	if (is_successful(envelope)) {
		if (hooks.on_completed) {
			hooks.on_completed(envelope);
		}
		return;
	}

	if (hooks.on_failed) {
		hooks.on_failed(envelope);
	}
}

}  // namespace

namespace dasall::tools {

ToolManager::ToolManager()
		: ToolManager(default_dependencies()) {}

ToolManager::ToolManager(manager::ToolManagerDependencies dependencies)
		: dependencies_(std::move(dependencies)) {
	const auto defaults = default_dependencies();
	if (!dependencies_.registry) {
		dependencies_.registry = defaults.registry;
	}
	if (!dependencies_.validator) {
		dependencies_.validator = defaults.validator;
	}
	if (!dependencies_.config_adapter) {
		dependencies_.config_adapter = defaults.config_adapter;
	}
	if (!dependencies_.policy_gate) {
		dependencies_.policy_gate = defaults.policy_gate;
	}
	if (!dependencies_.route_selector) {
		dependencies_.route_selector = defaults.route_selector;
	}
	if (!dependencies_.build_manifest.has_consistent_values()) {
		dependencies_.build_manifest = defaults.build_manifest;
	}
}

ToolInvocationEnvelope ToolManager::invoke(
		const contracts::ToolRequest& request,
		const ToolInvocationContext& context) {
	if (dependencies_.audit_hooks.on_requested) {
		dependencies_.audit_hooks.on_requested(request, context);
	}

	const auto envelope = run_invoke_pipeline(request, context);
	emit_terminal_audit(dependencies_.audit_hooks, envelope);
	return envelope;
}

std::vector<ToolInvocationEnvelope> ToolManager::invoke_batch(
		std::span<const contracts::ToolRequest> requests,
		const ToolInvocationContext& context) {
	std::vector<ToolInvocationEnvelope> envelopes;
	envelopes.reserve(requests.size());

	for (const auto& request : requests) {
		envelopes.push_back(invoke(request, context));
	}

	return envelopes;
}

ToolInvocationEnvelope ToolManager::compensate(
		const CompensationRequest& request,
		const ToolInvocationContext& context) {
	const auto envelope = run_compensation_pipeline(request, context);
	if (dependencies_.audit_hooks.on_compensation) {
		dependencies_.audit_hooks.on_compensation(request, envelope);
	}
	emit_terminal_audit(dependencies_.audit_hooks, envelope);
	return envelope;
}

void ToolManager::set_route_health(route::ToolRouteHealthSnapshot health) {
	route_health_ = std::move(health);
}

void ToolManager::set_capability_snapshot(
		std::optional<CapabilitySnapshot> capability_snapshot) {
	capability_snapshot_ = std::move(capability_snapshot);
}

ToolInvocationEnvelope ToolManager::run_invoke_pipeline(
		const contracts::ToolRequest& request,
		const ToolInvocationContext& context) const {
	static_cast<void>(context);
	return build_failure_envelope(
			request,
			"tool.manager.pipeline_unconfigured",
			"tools.manager.invoke");
}

ToolInvocationEnvelope ToolManager::run_compensation_pipeline(
		const CompensationRequest& request,
		const ToolInvocationContext& context) const {
	static_cast<void>(context);
	return build_compensation_failure_envelope(
			request,
			"tool.manager.compensation_unconfigured",
			"tools.manager.compensate");
}

manager::ToolManagerDependencies ToolManager::default_dependencies() {
	return manager::ToolManagerDependencies{
			.registry = std::make_shared<registry::ToolRegistry>(),
			.validator = std::make_shared<validation::ToolValidator>(),
			.config_adapter = std::make_shared<config::ToolConfigAdapter>(),
			.policy_gate = std::make_shared<policy::ToolPolicyGate>(),
			.route_selector = std::make_shared<route::ToolRouteSelector>(),
			.build_manifest = profiles::BuildProfileManifest{
					.enabled_modules = {"runtime", "tools_builtin"},
					.enabled_adapters = {},
					.observability_level = "minimal",
					.build_tags = {"tools:skeleton"},
					.toolchain_hint = std::string("x86_64-linux-gnu"),
			},
			.executor = {},
			.projector = {},
			.audit_hooks = {},
	};
}

}  // namespace dasall::tools
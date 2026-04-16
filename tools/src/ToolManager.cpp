#include "ToolManager.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "execution/BuiltinExecutorLane.h"
#include "error/ResultCode.h"
#include "ops/ToolAuditBridge.h"
#include "projection/ResultProjector.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::contracts::ToolDescriptor;
using dasall::contracts::ToolIR;
using dasall::contracts::ToolIROperation;
using dasall::contracts::ToolIRRoute;
using dasall::contracts::ToolRequest;
using dasall::contracts::ToolResult;
using dasall::tools::CompensationRequest;
using dasall::tools::ToolCompensationHint;
using dasall::tools::ToolInvocationContext;
using dasall::tools::ToolInvocationEnvelope;
using dasall::tools::ToolRouteFacts;
using dasall::tools::manager::ToolAuditHooks;
using dasall::tools::manager::ToolExecutionRequest;

[[nodiscard]] std::int64_t current_time_ms() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
						 std::chrono::system_clock::now().time_since_epoch())
			.count();
}

[[nodiscard]] std::string route_kind_string(ToolIRRoute route) {
	switch (route) {
		case ToolIRRoute::LocalTool:
			return "builtin";
		case ToolIRRoute::WorkflowEngine:
			return "workflow";
		case ToolIRRoute::MCPRemote:
			return "mcp";
		case ToolIRRoute::Unspecified:
			break;
	}

	return "unspecified";
}

[[nodiscard]] const dasall::tools::projection::ResultProjector& standard_projector() {
	static const dasall::tools::projection::ResultProjector projector;
	return projector;
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

[[nodiscard]] ToolRouteFacts build_route_facts(
		const dasall::tools::route::ToolRouteDecision& route_decision) {
	ToolRouteFacts facts;
	facts.route_kind = route_kind_string(route_decision.route);
	facts.route_ref = route_decision.server_id.has_value()
								? route_decision.server_id
								: std::optional<std::string>(route_decision.lane_key);
	facts.decision_reason = route_decision.reason_code;
	facts.plugin_id = std::nullopt;
	facts.server_id = route_decision.server_id;
	return facts;
}

[[nodiscard]] ToolResult build_failure_result(
		const ToolRequest& request,
		ResultCode result_code,
		std::string reason_code,
		std::string stage) {
	return ToolResult{
			.request_id = request.request_id,
			.tool_call_id = request.tool_call_id,
			.tool_name = request.tool_name,
			.success = false,
			.payload = std::nullopt,
			.error = build_error(
					result_code,
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
		ResultCode result_code,
		std::string reason_code,
		std::string stage,
		std::optional<ToolRouteFacts> route_facts = std::nullopt) {
	ToolInvocationEnvelope envelope;
	envelope.tool_result = build_failure_result(
			request,
			result_code,
			reason_code,
			stage);
	envelope.failure_reason_code = std::move(reason_code);
	envelope.route_facts = std::move(route_facts);
	envelope.evidence_refs = std::vector<std::string>{
			"stage:" + std::move(stage),
			"reason:" + *envelope.failure_reason_code,
	};
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

[[nodiscard]] std::optional<std::string> derive_requested_domain(
		const ToolIR& tool_ir,
		const ToolDescriptor& descriptor) {
	if (tool_ir.route.has_value()) {
		switch (*tool_ir.route) {
			case ToolIRRoute::LocalTool:
				return std::string("builtin");
			case ToolIRRoute::WorkflowEngine:
				return std::string("workflow");
			case ToolIRRoute::MCPRemote:
				return std::string("mcp");
			case ToolIRRoute::Unspecified:
				break;
		}
	}

	if (!descriptor.category.has_value()) {
		return std::nullopt;
	}

	switch (*descriptor.category) {
		case dasall::contracts::ToolCategory::Workflow:
		case dasall::contracts::ToolCategory::AgentDelegation:
			return std::string("workflow");
		case dasall::contracts::ToolCategory::Information:
		case dasall::contracts::ToolCategory::Action:
		case dasall::contracts::ToolCategory::Diagnostic:
			return std::string("builtin");
		case dasall::contracts::ToolCategory::Unspecified:
			break;
	}

	return std::nullopt;
}

[[nodiscard]] bool has_confirmation_fact(const ToolInvocationContext& context) {
	if (!context.confirmation_facts.has_value()) {
		return false;
	}

	return std::any_of(
			context.confirmation_facts->begin(),
			context.confirmation_facts->end(),
			[](const dasall::tools::ToolConfirmationFact& fact) {
				return fact.has_consistent_values();
			});
}

[[nodiscard]] bool is_high_risk_descriptor(const ToolDescriptor& descriptor) {
	if (!descriptor.category.has_value()) {
		return false;
	}

	switch (*descriptor.category) {
		case dasall::contracts::ToolCategory::Action:
			return !descriptor.is_read_only.value_or(false);
		case dasall::contracts::ToolCategory::AgentDelegation:
			return true;
		case dasall::contracts::ToolCategory::Information:
		case dasall::contracts::ToolCategory::Workflow:
		case dasall::contracts::ToolCategory::Diagnostic:
		case dasall::contracts::ToolCategory::Unspecified:
			return false;
	}

	return false;
}

[[nodiscard]] ToolResult build_non_execution_result(
		const ToolIR& tool_ir,
		const dasall::tools::route::ToolRouteDecision& route_decision,
		std::string mode_tag) {
	return ToolResult{
			.request_id = tool_ir.request_id,
			.tool_call_id = tool_ir.tool_call_id,
			.tool_name = tool_ir.tool_name,
			.success = true,
			.payload = std::string("{\"status\":\"accepted\",\"mode\":\"") +
							mode_tag + "\",\"route\":\"" +
							route_kind_string(route_decision.route) + "\",\"lane\":\"" +
							route_decision.lane_key + "\"}",
			.error = std::nullopt,
			.side_effects = std::nullopt,
			.completed_at = current_time_ms(),
			.duration_ms = 0,
			.goal_id = tool_ir.goal_id,
			.worker_task_id = tool_ir.worker_task_id,
			.tags = std::vector<std::string>{std::move(mode_tag)},
	};
}

[[nodiscard]] ToolResult normalize_result(
		const ToolRequest& request,
		const ToolIR& tool_ir,
		ToolResult result) {
	if (!result.request_id.has_value()) {
		result.request_id = tool_ir.request_id.has_value() ? tool_ir.request_id : request.request_id;
	}
	if (!result.tool_call_id.has_value()) {
		result.tool_call_id = tool_ir.tool_call_id.has_value() ? tool_ir.tool_call_id
															 : request.tool_call_id;
	}
	if (!result.tool_name.has_value()) {
		result.tool_name = tool_ir.tool_name.has_value() ? tool_ir.tool_name : request.tool_name;
	}
	if (!result.goal_id.has_value()) {
		result.goal_id = tool_ir.goal_id.has_value() ? tool_ir.goal_id : request.goal_id;
	}
	if (!result.worker_task_id.has_value()) {
		result.worker_task_id = tool_ir.worker_task_id.has_value() ? tool_ir.worker_task_id
																  : request.worker_task_id;
	}
	if (!result.tags.has_value()) {
		result.tags = request.tags;
	}
	if (!result.success.has_value()) {
		result.success = !result.error.has_value();
	}
	if (result.success.value_or(false)) {
		result.error = std::nullopt;
	} else if (!result.error.has_value()) {
		result.error = build_error(
				ResultCode::ToolExecutionFailed,
				"tool.manager.execution_failed",
				"tools.manager.execute",
				result.tool_name.value_or(std::string("unknown_tool")));
	}
	if (!result.completed_at.has_value()) {
		result.completed_at = current_time_ms();
	}
	if (!result.duration_ms.has_value()) {
		result.duration_ms = 0;
	}
	return result;
}

[[nodiscard]] std::optional<std::vector<ToolCompensationHint>> build_compensation_hints(
		const ToolResult& result) {
	if (!result.side_effects.has_value() || result.side_effects->empty()) {
		return std::nullopt;
	}

	return std::vector<ToolCompensationHint>{ToolCompensationHint{
			.compensation_action = std::string("compensate.side_effects"),
			.target_ref = result.tool_call_id,
			.reason_code = std::string("tool.manager.compensation_available"),
			.evidence_refs = *result.side_effects,
	}};
}

[[nodiscard]] ToolResult default_executor(const ToolExecutionRequest& execution_request) {
	return ToolResult{
			.request_id = execution_request.tool_ir.request_id,
			.tool_call_id = execution_request.tool_ir.tool_call_id,
			.tool_name = execution_request.tool_ir.tool_name,
			.success = true,
			.payload = std::string("{\"status\":\"executed\",\"route\":\"") +
							route_kind_string(execution_request.route_decision.route) +
							"\",\"lane\":\"" + execution_request.route_decision.lane_key + "\"}",
			.error = std::nullopt,
			.side_effects = std::nullopt,
			.completed_at = current_time_ms(),
			.duration_ms = 1,
			.goal_id = execution_request.tool_ir.goal_id,
			.worker_task_id = execution_request.tool_ir.worker_task_id,
			.tags = std::vector<std::string>{"tool.executor.default"},
	};
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
	if (!dependencies_.executor) {
		dependencies_.executor = defaults.executor;
	}
	if (!dependencies_.projector) {
		dependencies_.projector = defaults.projector;
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
	if (!dependencies_.registry || !dependencies_.validator || !dependencies_.config_adapter ||
			!dependencies_.policy_gate || !dependencies_.route_selector ||
			!dependencies_.executor || !dependencies_.projector) {
		return build_failure_envelope(
				request,
				ResultCode::RuntimeRetryExhausted,
				"tool.manager.dependencies_unavailable",
				"tools.manager.init");
	}

	if (!context.has_profile_snapshot()) {
		return build_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				"tool.manager.profile_missing",
				"tools.manager.context");
	}

	if (!request.tool_name.has_value() || request.tool_name->empty()) {
		return build_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				"InvalidRequest",
				"tools.manager.resolve");
	}

	const auto descriptor = dependencies_.registry->resolve_descriptor(*request.tool_name);
	if (!descriptor.has_value()) {
		return build_failure_envelope(
				request,
				ResultCode::ToolExecutionFailed,
				"tool.manager.descriptor_missing",
				"tools.manager.resolve");
	}

	const auto validation_result = dependencies_.validator->validate(request, *descriptor);
	if (!validation_result.ok()) {
		return build_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				validation_result.diagnostics->error_code,
				"tools.manager.validate");
	}

	const auto& snapshot = *context.profile_snapshot;
	const auto policy_view =
			dependencies_.config_adapter->build_policy_view(snapshot, dependencies_.build_manifest);
	const auto timeout_view =
			dependencies_.config_adapter->build_timeout_view(snapshot, dependencies_.build_manifest);
	const auto requested_domain = derive_requested_domain(*validation_result.tool_ir, *descriptor);
	const auto admission_decision = dependencies_.policy_gate->evaluate(
			ToolAdmissionRequest{
					.tool_name = descriptor->tool_name.value_or(*request.tool_name),
					.required_scopes = descriptor->required_scopes.value_or(std::vector<std::string>{}),
					.caller_domain = requested_domain,
					.high_risk = is_high_risk_descriptor(*descriptor),
					.confirmation_present = has_confirmation_fact(context),
					.route_proven = requested_domain.has_value(),
			},
			policy_view);
	if (!admission_decision.allowed()) {
		return build_failure_envelope(
				request,
				ResultCode::PolicyDenied,
				admission_decision.reason_code,
				"tools.manager.admit");
	}

	const auto route_decision = dependencies_.route_selector->select_route(
			*validation_result.tool_ir,
			*descriptor,
			timeout_view,
			dependencies_.registry->list_mcp_bindings(*request.tool_name),
			capability_snapshot_,
			route_health_);
	if (!route_decision.available) {
		return build_failure_envelope(
				request,
				ResultCode::ToolExecutionFailed,
				route_decision.reason_code,
				"tools.manager.route",
				build_route_facts(route_decision));
	}

	ToolResult result;
	if (*validation_result.tool_ir->operation == ToolIROperation::DryRun) {
		result = build_non_execution_result(
				*validation_result.tool_ir,
				route_decision,
				"tool.mode.dry_run");
	} else if (*validation_result.tool_ir->operation == ToolIROperation::ValidateOnly) {
		result = build_non_execution_result(
				*validation_result.tool_ir,
				route_decision,
				"tool.mode.validate_only");
	} else {
		result = dependencies_.executor(ToolExecutionRequest{
					.tool_ir = *validation_result.tool_ir,
					.route_decision = route_decision,
					.invocation_context = context,
			});
	}

	auto normalized_result = normalize_result(
			request,
			*validation_result.tool_ir,
			std::move(result));
	auto envelope = dependencies_.projector(normalized_result, route_decision, context);
	envelope.tool_result = normalized_result;
	const auto fallback_projection =
			standard_projector().project(normalized_result, route_decision, context);
	if (!envelope.route_facts.has_value()) {
		envelope.route_facts = fallback_projection.route_facts;
	}
	if (!envelope.observation.has_value()) {
		envelope.observation = fallback_projection.observation;
	}
	if (!envelope.observation_digest.has_value()) {
		envelope.observation_digest = fallback_projection.observation_digest;
	}
	if (!envelope.evidence_refs.has_value()) {
		envelope.evidence_refs = fallback_projection.evidence_refs;
	}
	if (!envelope.failure_reason_code.has_value() &&
			!envelope.tool_result->success.value_or(false)) {
		envelope.failure_reason_code = fallback_projection.failure_reason_code;
	}
	if (envelope.tool_result->success.value_or(false)) {
		envelope.failure_reason_code = std::nullopt;
	}
	if (!envelope.compensation_hints.has_value() &&
			descriptor->supports_compensation.value_or(false)) {
		envelope.compensation_hints = build_compensation_hints(*envelope.tool_result);
	}
	return envelope;
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
	auto registry = std::make_shared<registry::ToolRegistry>();
	auto validator = std::make_shared<validation::ToolValidator>();
	auto config_adapter = std::make_shared<config::ToolConfigAdapter>();
	auto policy_gate = std::make_shared<policy::ToolPolicyGate>();
	auto route_selector = std::make_shared<route::ToolRouteSelector>();
	auto builtin_lane = std::make_shared<execution::BuiltinExecutorLane>(
			execution::BuiltinExecutorLaneDependencies{
					.registry = registry,
					.service_bridge = nullptr,
					.execution_service = nullptr,
					.data_service = nullptr,
					.now_ms = {},
			});
	auto audit_bridge = std::make_shared<ops::ToolAuditBridge>();
	auto result_projector = std::make_shared<projection::ResultProjector>();

	return manager::ToolManagerDependencies{
			.registry = std::move(registry),
			.validator = std::move(validator),
			.config_adapter = std::move(config_adapter),
			.policy_gate = std::move(policy_gate),
			.route_selector = std::move(route_selector),
			.build_manifest = profiles::BuildProfileManifest{
					.enabled_modules = {"runtime", "tools_builtin"},
					.enabled_adapters = {},
					.observability_level = "minimal",
					.build_tags = {"tools:manager"},
					.toolchain_hint = std::string("x86_64-linux-gnu"),
			},
			.executor = [builtin_lane](const ToolExecutionRequest& execution_request) {
					if (execution_request.route_decision.route == ToolIRRoute::LocalTool) {
						return builtin_lane->execute(
								execution_request.tool_ir,
								ToolExecutionContext{
										.invocation_context = execution_request.invocation_context,
										.lane_key = execution_request.route_decision.lane_key,
								});
					}

					return default_executor(execution_request);
			},
			.projector = [result_projector](
						const ToolResult& result,
						const dasall::tools::route::ToolRouteDecision& route_decision,
						const ToolInvocationContext& invocation_context) {
					return result_projector->project(result, route_decision, invocation_context);
			},
			.audit_hooks = ops::ToolAuditBridge::bind_hooks(audit_bridge),
	};
}

}  // namespace dasall::tools
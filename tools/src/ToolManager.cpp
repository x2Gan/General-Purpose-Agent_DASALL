#include "ToolManager.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "execution/WorkflowEngine.h"
#include "execution/BuiltinExecutorLane.h"
#include "error/ResultCode.h"
#include "ops/ToolAuditBridge.h"
#include "ops/ToolMetricsBridge.h"
#include "ops/ToolTraceBridge.h"
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
	envelope.evidence_refs = std::vector<std::string>{
			"stage:" + stage,
			"reason:" + reason_code,
	};
	envelope.failure_reason_code = std::move(reason_code);
	envelope.route_facts = std::move(route_facts);
	return envelope;
}

[[nodiscard]] ToolInvocationEnvelope build_compensation_failure_envelope(
		const CompensationRequest& request,
		ResultCode result_code,
		std::string reason_code,
		std::string stage,
		std::optional<ToolRouteFacts> route_facts = std::nullopt) {
	ToolInvocationEnvelope envelope;
	envelope.tool_result = ToolResult{
			.request_id = std::nullopt,
			.tool_call_id = request.tool_call_id,
			.tool_name = std::nullopt,
			.success = false,
			.payload = std::nullopt,
			.error = build_error(
					result_code,
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
	envelope.evidence_refs = request.evidence_refs;
	envelope.failure_reason_code = std::move(reason_code);
	envelope.route_facts = std::move(route_facts);
	return envelope;
}

[[nodiscard]] bool is_successful(const ToolInvocationEnvelope& envelope) {
	return envelope.tool_result.has_value() && envelope.tool_result->success.value_or(false);
}

[[nodiscard]] ResultCode result_code_for_envelope(
		const ToolInvocationEnvelope& envelope,
		ResultCode fallback = ResultCode::ToolExecutionFailed) {
	if (envelope.tool_result.has_value() && envelope.tool_result->error.has_value() &&
			envelope.tool_result->error->details.code.has_value()) {
		return static_cast<ResultCode>(*envelope.tool_result->error->details.code);
	}

	return fallback;
}

[[nodiscard]] std::string message_for_envelope(const ToolInvocationEnvelope& envelope) {
	if (envelope.tool_result.has_value() && envelope.tool_result->error.has_value() &&
			!envelope.tool_result->error->details.message.empty()) {
		return envelope.tool_result->error->details.message;
	}

	if (envelope.failure_reason_code.has_value()) {
		return *envelope.failure_reason_code;
	}

	return std::string("tool execution failed");
}

[[nodiscard]] std::string stage_for_envelope(const ToolInvocationEnvelope& envelope) {
	if (envelope.tool_result.has_value() && envelope.tool_result->error.has_value() &&
			!envelope.tool_result->error->details.stage.empty()) {
		return envelope.tool_result->error->details.stage;
	}

	return std::string("tools.manager.invoke");
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

	const auto capability_id = result.tool_name.value_or(std::string("unknown_tool"));
	const auto target_id =
			result.tool_call_id.value_or(std::string("builtin:") + capability_id);

	return std::vector<ToolCompensationHint>{ToolCompensationHint{
			.compensation_action = result.tool_name,
			.target_ref = dasall::tools::bridge::format_compensation_target_ref(
					capability_id,
					target_id),
			.reason_code = std::string("tool.manager.compensation_available"),
			.evidence_refs = *result.side_effects,
	}};
}

[[nodiscard]] dasall::contracts::ToolInvocationKind invocation_kind_for_category(
		const ToolDescriptor& descriptor) {
	if (!descriptor.category.has_value()) {
		return dasall::contracts::ToolInvocationKind::Action;
	}

	switch (*descriptor.category) {
		case dasall::contracts::ToolCategory::Information:
			return dasall::contracts::ToolInvocationKind::InformationQuery;
		case dasall::contracts::ToolCategory::Action:
			return dasall::contracts::ToolInvocationKind::Action;
		case dasall::contracts::ToolCategory::Workflow:
			return dasall::contracts::ToolInvocationKind::Workflow;
		case dasall::contracts::ToolCategory::AgentDelegation:
			return dasall::contracts::ToolInvocationKind::AgentDelegation;
		case dasall::contracts::ToolCategory::Diagnostic:
			return dasall::contracts::ToolInvocationKind::Diagnostic;
		case dasall::contracts::ToolCategory::Unspecified:
			break;
	}

	return dasall::contracts::ToolInvocationKind::Action;
}

void append_unique_evidence_refs(
		std::optional<std::vector<std::string>>* target,
		const std::optional<std::vector<std::string>>& source) {
	if (!source.has_value() || source->empty()) {
		return;
	}

	if (!target->has_value()) {
		target->emplace();
	}

	for (const auto& ref : *source) {
		if (ref.empty()) {
			continue;
		}
		auto& refs = target->value();
		if (std::find(refs.begin(), refs.end(), ref) == refs.end()) {
			refs.push_back(ref);
		}
	}
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
	if (!dependencies_.workflow_engine) {
		dependencies_.workflow_engine = defaults.workflow_engine;
	}
	if (!dependencies_.metrics_bridge) {
		dependencies_.metrics_bridge = defaults.metrics_bridge;
	}
	if (!dependencies_.trace_bridge) {
		dependencies_.trace_bridge = defaults.trace_bridge;
	}
	if (!dependencies_.build_manifest.has_consistent_values()) {
		dependencies_.build_manifest = defaults.build_manifest;
	}
	if (!dependencies_.executor) {
		dependencies_.executor = defaults.executor;
	}
	if (!dependencies_.compensation_executor) {
		dependencies_.compensation_executor = defaults.compensation_executor;
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
	ops::ToolTraceSpan root_trace_scope;
	if (dependencies_.trace_bridge) {
		root_trace_scope = dependencies_.trace_bridge->start_root_span(request, context);
	}

	std::optional<ToolDescriptor> descriptor_cache;
	auto emit_failure_with_metrics = [&](ToolInvocationEnvelope envelope) {
		if (dependencies_.metrics_bridge) {
			static_cast<void>(dependencies_.metrics_bridge->record_preflight_failure(
					request,
					descriptor_cache,
					envelope,
					context));
		}
		return envelope;
	};
	auto invoke_body = [&]() -> ToolInvocationEnvelope {
		if (!dependencies_.registry || !dependencies_.validator || !dependencies_.config_adapter ||
				!dependencies_.policy_gate || !dependencies_.route_selector ||
				!dependencies_.executor || !dependencies_.projector) {
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::RuntimeRetryExhausted,
					"tool.manager.dependencies_unavailable",
					"tools.manager.init"));
		}

		if (!context.has_profile_snapshot()) {
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::ValidationFieldMissing,
					"tool.manager.profile_missing",
					"tools.manager.context"));
		}

		if (!request.tool_name.has_value() || request.tool_name->empty()) {
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::ValidationFieldMissing,
					"InvalidRequest",
					"tools.manager.resolve"));
		}

		const auto descriptor = dependencies_.registry->resolve_descriptor(*request.tool_name);
		descriptor_cache = descriptor;
		if (!descriptor.has_value()) {
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::ToolExecutionFailed,
					"tool.manager.descriptor_missing",
					"tools.manager.resolve"));
		}

		const auto validation_result = [&]() {
			const auto validate = [&]() {
				return dependencies_.validator->validate(request, *descriptor);
			};

			if (!dependencies_.trace_bridge) {
				return validate();
			}

			auto validation_scope = dependencies_.trace_bridge->start_stage_span(
					"tool.validate",
					request,
					context);
			auto result = dependencies_.trace_bridge->with_span(validation_scope, validate);
			if (!result.ok()) {
				dependencies_.trace_bridge->mark_error(
						&validation_scope,
						ResultCode::ValidationFieldMissing,
						result.diagnostics->error_code,
						"tools.manager.validate");
			} else {
				dependencies_.trace_bridge->mark_success(&validation_scope);
			}
			return result;
		}();
		if (!validation_result.ok()) {
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::ValidationFieldMissing,
					validation_result.diagnostics->error_code,
					"tools.manager.validate"));
		}

		const auto& snapshot = *context.profile_snapshot;
		const auto policy_view = dependencies_.config_adapter->build_policy_view(
				snapshot,
				dependencies_.build_manifest);
		const auto timeout_view = dependencies_.config_adapter->build_timeout_view(
				snapshot,
				dependencies_.build_manifest);
		const auto requested_domain =
				derive_requested_domain(*validation_result.tool_ir, *descriptor);
		const auto admission_decision = [&]() {
			const auto evaluate = [&]() {
				return dependencies_.policy_gate->evaluate(
						ToolAdmissionRequest{
								.tool_name = descriptor->tool_name.value_or(*request.tool_name),
								.required_scopes = descriptor->required_scopes.value_or(
										std::vector<std::string>{}),
								.caller_domain = requested_domain,
								.high_risk = is_high_risk_descriptor(*descriptor),
								.confirmation_present = has_confirmation_fact(context),
								.route_proven = requested_domain.has_value(),
						},
						policy_view);
			};

			if (!dependencies_.trace_bridge) {
				return evaluate();
			}

			auto policy_scope = dependencies_.trace_bridge->start_stage_span(
					"tool.policy",
					request,
					context);
			auto decision = dependencies_.trace_bridge->with_span(policy_scope, evaluate);
			if (policy_scope.is_valid()) {
				policy_scope.span->set_attribute("tools.reason_code", decision.reason_code);
			}
			if (!decision.allowed()) {
				dependencies_.trace_bridge->mark_error(
						&policy_scope,
						ResultCode::PolicyDenied,
						decision.reason_code,
						"tools.manager.admit");
			} else {
				dependencies_.trace_bridge->mark_success(&policy_scope);
			}
			return decision;
		}();
		if (!admission_decision.allowed()) {
			if (dependencies_.metrics_bridge) {
				static_cast<void>(dependencies_.metrics_bridge->record_admission_denied(
						request,
						descriptor_cache,
						admission_decision.reason_code,
						context));
			}
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::PolicyDenied,
					admission_decision.reason_code,
					"tools.manager.admit"));
		}

		const auto route_decision = [&]() {
			const auto select = [&]() {
				return dependencies_.route_selector->select_route(
						*validation_result.tool_ir,
						*descriptor,
						timeout_view,
						dependencies_.registry->list_mcp_bindings(*request.tool_name),
						capability_snapshot_,
						route_health_);
			};

			if (!dependencies_.trace_bridge) {
				return select();
			}

			auto route_scope = dependencies_.trace_bridge->start_stage_span(
					"tool.route",
					request,
					context);
			auto decision = dependencies_.trace_bridge->with_span(route_scope, select);
			if (route_scope.is_valid()) {
				route_scope.span->set_attribute("tools.reason_code", decision.reason_code);
				route_scope.span->set_attribute(
						"tools.route_kind",
						infra::tracing::TraceAttributeValue{route_kind_string(decision.route)});
				route_scope.span->set_attribute(
						"tools.lane_key",
						infra::tracing::TraceAttributeValue{decision.lane_key});
				if (decision.server_id.has_value()) {
					route_scope.span->set_attribute(
							"tools.server_id",
							infra::tracing::TraceAttributeValue{*decision.server_id});
				}
			}
			if (!decision.available) {
				dependencies_.trace_bridge->mark_error(
						&route_scope,
						ResultCode::ToolExecutionFailed,
						decision.reason_code,
						"tools.manager.route");
			} else {
				dependencies_.trace_bridge->mark_success(&route_scope);
			}
			return decision;
		}();
		if (!route_decision.available) {
			return emit_failure_with_metrics(build_failure_envelope(
					request,
					ResultCode::ToolExecutionFailed,
					route_decision.reason_code,
					"tools.manager.route",
					build_route_facts(route_decision)));
		}

		if (dependencies_.metrics_bridge) {
			static_cast<void>(dependencies_.metrics_bridge->record_route_selection(
					request,
					descriptor_cache,
					route_decision,
					context));
		}

		ToolResult result;
		std::optional<std::vector<ToolCompensationHint>> workflow_compensation_hints;
		std::optional<std::vector<std::string>> workflow_evidence_refs;
		std::optional<std::string> workflow_failure_reason_code;
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
		} else if (route_decision.route == ToolIRRoute::WorkflowEngine) {
			if (!dependencies_.workflow_engine) {
				return emit_failure_with_metrics(build_failure_envelope(
						request,
						ResultCode::ToolExecutionFailed,
						"workflow.engine_unavailable",
						"tools.manager.execute",
						build_route_facts(route_decision)));
			}

			auto outcome = dependencies_.workflow_engine->execute(
					*validation_result.tool_ir,
					ToolExecutionContext{
							.invocation_context = context,
							.lane_key = route_decision.lane_key,
					});
			result = std::move(outcome.tool_result);
			if (outcome.compensation_hints.has_value()) {
				workflow_compensation_hints = std::move(outcome.compensation_hints);
			}
			if (!outcome.evidence_refs.empty()) {
				workflow_evidence_refs = std::move(outcome.evidence_refs);
			}
			if (!outcome.failure_reason_code.empty()) {
				workflow_failure_reason_code = std::move(outcome.failure_reason_code);
			}
		} else {
			const auto execute = [&]() {
				return dependencies_.executor(ToolExecutionRequest{
							.tool_ir = *validation_result.tool_ir,
							.route_decision = route_decision,
							.invocation_context = context,
					});
			};

			if (!dependencies_.trace_bridge || route_decision.route != ToolIRRoute::LocalTool) {
				result = execute();
			} else {
				auto execute_scope = dependencies_.trace_bridge->start_stage_span(
						"tool.execute.builtin",
						request,
						context,
						ops::ToolTraceStageDetails{
								.route_kind = std::string("builtin"),
								.lane_key = route_decision.lane_key,
								.server_id = route_decision.server_id,
								.reason_code = route_decision.reason_code,
						});
				result = dependencies_.trace_bridge->with_span(execute_scope, execute);
				if (result.error.has_value()) {
					dependencies_.trace_bridge->mark_error(
							&execute_scope,
							result.error->details.code.has_value()
									? static_cast<ResultCode>(*result.error->details.code)
									: ResultCode::ToolExecutionFailed,
							result.error->details.message,
							result.error->details.stage.empty()
									? std::string("tools.manager.execute")
									: result.error->details.stage);
				} else {
					dependencies_.trace_bridge->mark_success(&execute_scope);
				}
			}
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
			if (workflow_evidence_refs.has_value()) {
				envelope.evidence_refs = std::move(workflow_evidence_refs);
			}
			if (workflow_failure_reason_code.has_value()) {
				envelope.failure_reason_code = std::move(workflow_failure_reason_code);
			}
		if (envelope.tool_result->success.value_or(false)) {
			envelope.failure_reason_code = std::nullopt;
		}
			if (workflow_compensation_hints.has_value()) {
				envelope.compensation_hints = std::move(workflow_compensation_hints);
			} else if (!envelope.compensation_hints.has_value() &&
					route_decision.route != ToolIRRoute::WorkflowEngine &&
				descriptor->supports_compensation.value_or(false)) {
			envelope.compensation_hints = build_compensation_hints(*envelope.tool_result);
		}
		if (dependencies_.metrics_bridge) {
			static_cast<void>(dependencies_.metrics_bridge->record_execution_terminal(
					request,
					descriptor_cache,
					envelope,
					context));
		}
		return envelope;
	};

	ToolInvocationEnvelope envelope;
	if (dependencies_.trace_bridge && root_trace_scope.is_valid()) {
		envelope = dependencies_.trace_bridge->with_span(root_trace_scope, invoke_body);
	} else {
		envelope = invoke_body();
	}

	if (dependencies_.trace_bridge) {
		if (is_successful(envelope)) {
			dependencies_.trace_bridge->mark_success(&root_trace_scope);
		} else {
			dependencies_.trace_bridge->mark_error(
					&root_trace_scope,
					result_code_for_envelope(envelope),
					message_for_envelope(envelope),
					stage_for_envelope(envelope));
		}
	}

	return envelope;
}

ToolInvocationEnvelope ToolManager::run_compensation_pipeline(
		const CompensationRequest& request,
		const ToolInvocationContext& context) const {
	if (!dependencies_.registry || !dependencies_.validator || !dependencies_.config_adapter ||
				!dependencies_.policy_gate || !dependencies_.route_selector ||
				!dependencies_.compensation_executor || !dependencies_.projector) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::RuntimeRetryExhausted,
				"tool.manager.dependencies_unavailable",
				"tools.manager.compensate");
	}

	if (!context.has_profile_snapshot()) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				"tool.manager.profile_missing",
				"tools.manager.compensate");
	}

	if (!request.tool_call_id.has_value() || request.tool_call_id->empty() ||
				!request.compensation_action.has_value() || request.compensation_action->empty() ||
				!request.target_ref.has_value() || request.target_ref->empty() ||
				!request.reason_code.has_value() || request.reason_code->empty()) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				"InvalidRequest",
				"tools.manager.compensate");
	}

	const auto target = bridge::resolve_compensation_target(*request.target_ref);
	if (!target.has_value() || target->capability_id.empty()) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				"tool.manager.compensation_target_invalid",
				"tools.manager.compensate");
	}

	const auto descriptor = dependencies_.registry->resolve_descriptor(target->capability_id);
	if (!descriptor.has_value()) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ToolExecutionFailed,
				"tool.manager.descriptor_missing",
				"tools.manager.resolve");
	}
	if (!descriptor->supports_compensation.value_or(false) ||
				!descriptor->category.has_value() ||
				*descriptor->category != contracts::ToolCategory::Action) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ToolExecutionFailed,
				"tool.manager.compensation_unsupported",
				"tools.manager.compensate");
	}

	const auto synthetic_request = ToolRequest{
			.request_id = *request.tool_call_id + ".compensate",
			.tool_call_id = request.tool_call_id,
			.tool_name = target->capability_id,
			.invocation_kind = invocation_kind_for_category(*descriptor),
			.arguments_payload = std::string("{}"),
			.created_at = current_time_ms(),
			.goal_id = std::nullopt,
			.worker_task_id = std::nullopt,
			.runtime_budget = context.profile_snapshot->runtime_budget(),
			.timeout_ms = descriptor->default_timeout_ms,
			.idempotency_key = request.tool_call_id,
			.tags = std::vector<std::string>{"tool.compensation"},
	};
	const auto tool_ir = dependencies_.validator->inject_defaults(
			synthetic_request,
			*descriptor,
			ToolIROperation::Invoke,
			std::string("{}"));
	const auto tool_ir_guard = contracts::validate_tool_ir_field_rules(tool_ir);
	if (!tool_ir_guard.ok) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ValidationFieldMissing,
				"InvalidToolIR",
				"tools.manager.compensate");
	}

	const auto& snapshot = *context.profile_snapshot;
	const auto policy_view = dependencies_.config_adapter->build_policy_view(
			snapshot,
			dependencies_.build_manifest);
	const auto timeout_view = dependencies_.config_adapter->build_timeout_view(
			snapshot,
			dependencies_.build_manifest);
	const auto requested_domain = derive_requested_domain(tool_ir, *descriptor);
	const auto admission_decision = dependencies_.policy_gate->evaluate(
			ToolAdmissionRequest{
					.tool_name = descriptor->tool_name.value_or(target->capability_id),
					.required_scopes = descriptor->required_scopes.value_or(
							std::vector<std::string>{}),
					.caller_domain = requested_domain,
					.high_risk = is_high_risk_descriptor(*descriptor),
					.confirmation_present = has_confirmation_fact(context),
					.route_proven = requested_domain.has_value(),
			},
			policy_view);
	if (!admission_decision.allowed()) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::PolicyDenied,
				admission_decision.reason_code,
				"tools.manager.admit");
	}

	const auto route_decision = dependencies_.route_selector->select_route(
			tool_ir,
			*descriptor,
			timeout_view,
			dependencies_.registry->list_mcp_bindings(target->capability_id),
			capability_snapshot_,
			route_health_);
	if (!route_decision.available) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ToolExecutionFailed,
				route_decision.reason_code,
				"tools.manager.route",
				build_route_facts(route_decision));
	}
	if (route_decision.route != ToolIRRoute::LocalTool) {
		return build_compensation_failure_envelope(
				request,
				ResultCode::ToolExecutionFailed,
				"tool.manager.compensation_route_unsupported",
				"tools.manager.route",
				build_route_facts(route_decision));
	}

	auto result = dependencies_.compensation_executor(
			tool_ir,
			request,
			ToolExecutionContext{
					.invocation_context = context,
					.lane_key = route_decision.lane_key,
			});
	auto normalized_result = normalize_result(
			synthetic_request,
			tool_ir,
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
	append_unique_evidence_refs(&envelope.evidence_refs, request.evidence_refs);
	if (!envelope.failure_reason_code.has_value() &&
				!envelope.tool_result->success.value_or(false)) {
		envelope.failure_reason_code = fallback_projection.failure_reason_code;
	}
	if (envelope.tool_result->success.value_or(false)) {
		envelope.failure_reason_code = std::nullopt;
	}
	return envelope;
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
	auto metrics_bridge = std::make_shared<ops::ToolMetricsBridge>(
			nullptr,
			ops::ToolMetricsBridgeOptions{
					.enabled = false,
					.profile_id = std::string("unknown"),
					.metrics_granularity = std::string("minimal"),
					.meter_scope_name = std::string("tools"),
					.meter_scope_version = std::string("v1"),
					.now_ms = {},
			});
	auto trace_bridge = std::make_shared<ops::ToolTraceBridge>(
			nullptr,
			ops::ToolTraceBridgeOptions{
					.enabled = false,
					.profile_id = std::string("unknown"),
					.trace_sample_ratio = 0.0,
					.tracer_scope_name = std::string("tools"),
					.tracer_scope_version = std::string("v1"),
					.schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
			});
	auto result_projector = std::make_shared<projection::ResultProjector>();
		auto workflow_engine = std::make_shared<execution::WorkflowEngine>(
				execution::WorkflowEngineDependencies{
						.plan_loader = {},
						.builtin_executor = [builtin_lane](
								const ToolIR& tool_ir,
								const ToolExecutionContext& execution_context) {
							return builtin_lane->execute(tool_ir, execution_context);
						},
						.mcp_executor = {},
					});

	return manager::ToolManagerDependencies{
			.registry = std::move(registry),
			.validator = std::move(validator),
			.config_adapter = std::move(config_adapter),
			.policy_gate = std::move(policy_gate),
			.route_selector = std::move(route_selector),
				.workflow_engine = std::move(workflow_engine),
			.metrics_bridge = std::move(metrics_bridge),
			.trace_bridge = std::move(trace_bridge),
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
			.compensation_executor = [builtin_lane](
						const ToolIR& tool_ir,
						const CompensationRequest& request,
						const ToolExecutionContext& execution_context) {
					return builtin_lane->dispatch_compensation(
							tool_ir,
							request,
							execution_context);
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
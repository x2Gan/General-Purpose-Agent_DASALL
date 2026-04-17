#include "execution/BuiltinExecutorLane.h"

#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::contracts::ToolCategory;
using dasall::contracts::ToolIR;
using dasall::contracts::ToolResult;
using dasall::services::DataCatalogRequest;
using dasall::services::DataCatalogResult;
using dasall::services::DataQueryRequest;
using dasall::services::DataQueryResult;
using dasall::services::ExecutionCommandRequest;
using dasall::services::ExecutionCommandResult;
using dasall::services::ExecutionCompensationRequest;
using dasall::services::ExecutionDiagnoseRequest;
using dasall::services::ExecutionDiagnoseResult;
using dasall::services::ExecutionQueryRequest;
using dasall::services::ExecutionQueryResult;
using dasall::services::ExecutionSubscriptionRequest;
using dasall::services::ExecutionSubscriptionResult;
using dasall::tools::ToolExecutionContext;

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
      .retryable = result_code == ResultCode::ProviderTimeout ||
                   result_code == ResultCode::RuntimeRetryExhausted,
      .safe_to_replan = result_code != ResultCode::ProviderTimeout,
      .details = ErrorDetails{
          .code = static_cast<int>(result_code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = ErrorSourceRefMinimal{
          .ref_type = "builtin_executor_lane",
          .ref_id = std::move(ref_id),
      },
  };
}

[[nodiscard]] std::vector<std::string> build_tags(
    std::string dispatch_kind,
    const ToolExecutionContext& execution_context) {
  std::vector<std::string> tags{std::string("tool.executor.builtin"), std::move(dispatch_kind)};
  if (execution_context.lane_key.has_value() && !execution_context.lane_key->empty()) {
    tags.push_back(std::string("lane:") + *execution_context.lane_key);
  }
  return tags;
}

[[nodiscard]] ToolResult build_failure_result(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context,
    ResultCode result_code,
    std::string message,
    std::string stage) {
  return ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = false,
      .payload = std::nullopt,
      .error = build_error(
          result_code,
          std::move(message),
          std::move(stage),
          tool_ir.tool_name.value_or(std::string("unknown_tool"))),
      .side_effects = std::nullopt,
      .completed_at = current_time_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = build_tags(std::string("failure"), execution_context),
  };
}

class DefaultExecutionService final : public dasall::services::IExecutionService {
 public:
  ExecutionCommandResult execute(const ExecutionCommandRequest& request) override {
    return ExecutionCommandResult{
        .code = ResultCode::ToolExecutionFailed,
        .execution_id = std::string("builtin.exec:") + request.target.target_id,
        .payload_json = std::string("{\"status\":\"executed\",\"action\":\"") +
                        request.action + "\",\"target\":\"" + request.target.target_id +
                        "\"}",
        .side_effects = {},
        .compensation_hints = {},
        .error = std::nullopt,
    };
  }

  ExecutionCommandResult compensate(const ExecutionCompensationRequest& request) override {
    return ExecutionCommandResult{
        .code = ResultCode::ToolExecutionFailed,
        .execution_id = request.source_execution_id,
        .payload_json = std::string(),
        .side_effects = {},
        .compensation_hints = {},
        .error = build_error(ResultCode::ToolExecutionFailed,
                             "builtin.executor.compensation_unconfigured",
                             "tools.builtin.compensate",
                             request.target.capability_id),
    };
  }

  ExecutionQueryResult query_state(const ExecutionQueryRequest& request) override {
    return ExecutionQueryResult{
        .code = ResultCode::ToolExecutionFailed,
        .state = std::string(),
        .snapshot_json = std::string(),
        .from_cache = false,
        .error = build_error(ResultCode::ToolExecutionFailed,
                             "builtin.executor.query_state_unconfigured",
                             "tools.builtin.query_state",
                             request.target.capability_id),
    };
  }

  ExecutionSubscriptionResult subscribe(const ExecutionSubscriptionRequest& request) override {
    return ExecutionSubscriptionResult{
        .code = ResultCode::ToolExecutionFailed,
        .events_json = std::string(),
        .next_cursor = std::nullopt,
        .resync_required = false,
        .dropped_count = 0U,
        .error = build_error(ResultCode::ToolExecutionFailed,
                             "builtin.executor.subscribe_unconfigured",
                             "tools.builtin.subscribe",
                             request.target.capability_id),
    };
  }

  ExecutionDiagnoseResult diagnose(const ExecutionDiagnoseRequest& request) override {
    return ExecutionDiagnoseResult{
        .code = ResultCode::ToolExecutionFailed,
        .target_reachable = true,
        .report_json = std::string("{\"status\":\"reachable\",\"capability\":\"") +
                       request.target.capability_id + "\"}",
        .error = std::nullopt,
    };
  }
};

class DefaultDataService final : public dasall::services::IDataService {
 public:
  DataQueryResult query(const DataQueryRequest& request) override {
    return DataQueryResult{
        .code = ResultCode::ToolExecutionFailed,
        .rows_json = std::string("{\"dataset\":\"") + request.dataset +
                     "\",\"projection\":\"" + request.projection + "\"}",
        .from_cache = request.freshness == dasall::services::ServiceDataFreshness::allow_stale,
        .error = std::nullopt,
    };
  }

  DataCatalogResult list_capabilities(const DataCatalogRequest& request) override {
    return DataCatalogResult{
        .code = ResultCode::ToolExecutionFailed,
        .catalog_json = std::string("{\"target_class\":\"") + request.target_class + "\"}",
        .error = std::nullopt,
    };
  }
};

}  // namespace

namespace dasall::tools::execution {

BuiltinExecutorLane::BuiltinExecutorLane()
    : BuiltinExecutorLane(default_dependencies()) {}

BuiltinExecutorLane::BuiltinExecutorLane(BuiltinExecutorLaneDependencies dependencies)
    : dependencies_(std::move(dependencies)) {
  const auto defaults = default_dependencies();
  if (!dependencies_.registry) {
    dependencies_.registry = defaults.registry;
  }
  if (!dependencies_.service_bridge) {
    dependencies_.service_bridge = defaults.service_bridge;
  }
  if (!dependencies_.execution_service) {
    dependencies_.execution_service = defaults.execution_service;
  }
  if (!dependencies_.data_service) {
    dependencies_.data_service = defaults.data_service;
  }
  if (!dependencies_.now_ms) {
    dependencies_.now_ms = defaults.now_ms;
  }
}

ToolResult BuiltinExecutorLane::execute(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context) const {
  if (!dependencies_.registry || !dependencies_.service_bridge || !dependencies_.now_ms) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                "builtin.executor.dependencies_unavailable",
                                "tools.builtin.execute");
  }

  if (!tool_ir.tool_name.has_value() || tool_ir.tool_name->empty()) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ValidationFieldMissing,
                                "builtin.executor.tool_name_missing",
                                "tools.builtin.execute");
  }

  const auto descriptor = dependencies_.registry->resolve_descriptor(*tool_ir.tool_name);
  if (!descriptor.has_value() || !descriptor->category.has_value()) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                "builtin.executor.descriptor_missing",
                                "tools.builtin.resolve");
  }

  const auto start_ms = dependencies_.now_ms();
  const auto deadline_ms = tool_ir.timeout_ms.has_value()
      ? std::optional<std::int64_t>(start_ms + static_cast<std::int64_t>(*tool_ir.timeout_ms))
      : std::nullopt;

  if (deadline_ms.has_value() && start_ms >= *deadline_ms) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ProviderTimeout,
                                "builtin.executor.deadline_already_expired",
                                "tools.builtin.deadline");
  }

  ToolResult result;
  switch (*descriptor->category) {
    case ToolCategory::Action:
      result = dispatch_action(tool_ir, execution_context);
      break;
    case ToolCategory::Information:
      result = dispatch_query(tool_ir, execution_context);
      break;
    case ToolCategory::Diagnostic:
      result = dispatch_diagnose(tool_ir, execution_context);
      break;
    case ToolCategory::Workflow:
    case ToolCategory::AgentDelegation:
    case ToolCategory::Unspecified:
      return build_failure_result(tool_ir,
                                  execution_context,
                                  ResultCode::ToolExecutionFailed,
                                  "builtin.executor.unsupported_category",
                                  "tools.builtin.dispatch");
  }

  const auto end_ms = dependencies_.now_ms();
  result.duration_ms = static_cast<std::uint32_t>(end_ms - start_ms);

  if (deadline_ms.has_value() && end_ms > *deadline_ms) {
    result.success = false;
    result.error = build_error(
        ResultCode::ProviderTimeout,
        "builtin.executor.deadline_exceeded",
        "tools.builtin.deadline",
        tool_ir.tool_name.value_or(std::string("unknown_tool")));
    auto tags = result.tags.value_or(std::vector<std::string>{});
    tags.push_back("deadline:exceeded");
    result.tags = std::move(tags);
  }

  return result;
}

ToolResult BuiltinExecutorLane::dispatch_action(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context) const {
  if (!dependencies_.execution_service) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                "builtin.executor.execution_service_missing",
                                "tools.builtin.action");
  }

  try {
    const auto request = dependencies_.service_bridge->build_action_request(
        tool_ir,
        execution_context.invocation_context);
    return map_service_result(tool_ir,
                              execution_context,
                              dependencies_.execution_service->execute(request));
  } catch (const std::exception& ex) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                std::string("builtin.executor.action_exception:") + ex.what(),
                                "tools.builtin.action");
  }
}

ToolResult BuiltinExecutorLane::dispatch_query(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context) const {
  if (!dependencies_.data_service) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                "builtin.executor.data_service_missing",
                                "tools.builtin.query");
  }

  try {
    const auto request = dependencies_.service_bridge->build_query_request(
        tool_ir,
        execution_context.invocation_context);
    return map_service_result(tool_ir,
                              execution_context,
                              dependencies_.data_service->query(request));
  } catch (const std::exception& ex) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                std::string("builtin.executor.query_exception:") + ex.what(),
                                "tools.builtin.query");
  }
}

ToolResult BuiltinExecutorLane::dispatch_diagnose(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context) const {
  if (!dependencies_.execution_service) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                "builtin.executor.execution_service_missing",
                                "tools.builtin.diagnose");
  }

  try {
    const auto request = dependencies_.service_bridge->build_diagnose_request(
        tool_ir,
        execution_context.invocation_context);
    return map_service_result(tool_ir,
                              execution_context,
                              dependencies_.execution_service->diagnose(request));
  } catch (const std::exception& ex) {
    return build_failure_result(tool_ir,
                                execution_context,
                                ResultCode::ToolExecutionFailed,
                                std::string("builtin.executor.diagnose_exception:") + ex.what(),
                                "tools.builtin.diagnose");
  }
}

ToolResult BuiltinExecutorLane::map_service_result(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context,
    const ExecutionCommandResult& result) const {
  return ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = !result.error.has_value(),
      .payload = result.payload_json.empty() ? std::nullopt
                                             : std::optional<std::string>(result.payload_json),
      .error = result.error,
      .side_effects = result.side_effects.empty()
                          ? std::nullopt
                          : std::optional<std::vector<std::string>>(result.side_effects),
      .completed_at = dependencies_.now_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = build_tags(std::string("action"), execution_context),
  };
}

ToolResult BuiltinExecutorLane::map_service_result(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context,
    const DataQueryResult& result) const {
  auto tags = build_tags(std::string("query"), execution_context);
  if (result.from_cache) {
    tags.push_back("cache:hit");
  }

  return ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = !result.error.has_value(),
      .payload = result.rows_json.empty() ? std::nullopt
                                          : std::optional<std::string>(result.rows_json),
      .error = result.error,
      .side_effects = std::nullopt,
      .completed_at = dependencies_.now_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = std::move(tags),
  };
}

ToolResult BuiltinExecutorLane::map_service_result(
    const ToolIR& tool_ir,
    const ToolExecutionContext& execution_context,
    const ExecutionDiagnoseResult& result) const {
  return ToolResult{
      .request_id = tool_ir.request_id,
      .tool_call_id = tool_ir.tool_call_id,
      .tool_name = tool_ir.tool_name,
      .success = !result.error.has_value(),
      .payload = result.report_json.empty() ? std::nullopt
                                            : std::optional<std::string>(result.report_json),
      .error = result.error,
      .side_effects = std::nullopt,
      .completed_at = dependencies_.now_ms(),
      .duration_ms = 0,
      .goal_id = tool_ir.goal_id,
      .worker_task_id = tool_ir.worker_task_id,
      .tags = build_tags(std::string("diagnose"), execution_context),
  };
}

BuiltinExecutorLaneDependencies BuiltinExecutorLane::default_dependencies() {
  return BuiltinExecutorLaneDependencies{
      .registry = std::make_shared<registry::ToolRegistry>(),
      .service_bridge = std::make_shared<bridge::ToolServiceBridge>(),
      .execution_service = std::make_shared<DefaultExecutionService>(),
      .data_service = std::make_shared<DefaultDataService>(),
      .now_ms = current_time_ms,
  };
}

}  // namespace dasall::tools::execution
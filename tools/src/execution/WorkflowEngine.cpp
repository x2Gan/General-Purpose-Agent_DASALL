#include "execution/WorkflowEngine.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "ToolManager.h"
#include "error/ResultCode.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::contracts::ToolIR;
using dasall::contracts::ToolIRRoute;
using dasall::contracts::ToolResult;
using dasall::tools::ToolCompensationHint;
using dasall::tools::ToolExecutionContext;
using dasall::tools::execution::WorkflowBatch;
using dasall::tools::execution::WorkflowBatchBuildResult;
using dasall::tools::execution::WorkflowDelegationMode;
using dasall::tools::execution::WorkflowDelegationSidecar;
using dasall::tools::execution::WorkflowEngineDependencies;
using dasall::tools::execution::WorkflowExecutionOutcome;
using dasall::tools::execution::WorkflowPlan;
using dasall::tools::execution::WorkflowStep;
using dasall::tools::execution::WorkflowStepDispatchResult;
using dasall::tools::execution::WorkflowStepKind;
using dasall::tools::execution::WorkflowStepReceipt;

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] ErrorInfo build_error(
    ResultCode code,
    std::string message,
    std::string stage,
    std::string ref_id) {
  return ErrorInfo{
      .failure_type = dasall::contracts::classify_result_code(code),
      .retryable = false,
      .safe_to_replan = true,
      .details = ErrorDetails{
          .code = static_cast<int>(code),
          .message = std::move(message),
          .stage = std::move(stage),
      },
      .source_ref = ErrorSourceRefMinimal{
          .ref_type = "workflow_engine",
          .ref_id = std::move(ref_id),
      },
  };
}

[[nodiscard]] std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

void skip_ascii_ws(std::string_view text, std::size_t& cursor) {
  while (cursor < text.size() &&
         std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
    ++cursor;
  }
}

[[nodiscard]] std::optional<std::string> parse_json_string_token(
    std::string_view text,
    std::size_t& cursor) {
  if (cursor >= text.size() || text[cursor] != '"') {
    return std::nullopt;
  }

  std::string value;
  ++cursor;
  while (cursor < text.size()) {
    const char character = text[cursor++];
    if (character == '\\') {
      if (cursor >= text.size()) {
        return std::nullopt;
      }
      value.push_back(text[cursor++]);
      continue;
    }
    if (character == '"') {
      return value;
    }
    value.push_back(character);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> parse_json_value_token(
    std::string_view text,
    std::size_t& cursor) {
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size()) {
    return std::nullopt;
  }

  if (text[cursor] == '"') {
    const auto parsed = parse_json_string_token(text, cursor);
    if (!parsed.has_value()) {
      return std::nullopt;
    }
    return std::string("\"") + escape_json(*parsed) + "\"";
  }

  const std::size_t begin = cursor;
  int nested_braces = 0;
  int nested_brackets = 0;
  while (cursor < text.size()) {
    const char character = text[cursor];
    if (character == '{') {
      ++nested_braces;
    } else if (character == '}') {
      if (nested_braces == 0 && nested_brackets == 0) {
        break;
      }
      --nested_braces;
    } else if (character == '[') {
      ++nested_brackets;
    } else if (character == ']') {
      --nested_brackets;
    } else if (character == ',' && nested_braces == 0 && nested_brackets == 0) {
      break;
    }
    ++cursor;
  }

  while (cursor > begin &&
         std::isspace(static_cast<unsigned char>(text[cursor - 1])) != 0) {
    --cursor;
  }
  return std::string(text.substr(begin, cursor - begin));
}

[[nodiscard]] std::optional<std::map<std::string, std::string>> parse_flat_json_object(
    std::string_view text) {
  std::size_t cursor = 0;
  skip_ascii_ws(text, cursor);
  if (cursor >= text.size() || text[cursor] != '{') {
    return std::nullopt;
  }
  ++cursor;

  std::map<std::string, std::string> values;
  while (cursor < text.size()) {
    skip_ascii_ws(text, cursor);
    if (cursor < text.size() && text[cursor] == '}') {
      ++cursor;
      return values;
    }

    const auto key = parse_json_string_token(text, cursor);
    if (!key.has_value()) {
      return std::nullopt;
    }

    skip_ascii_ws(text, cursor);
    if (cursor >= text.size() || text[cursor] != ':') {
      return std::nullopt;
    }
    ++cursor;

    const auto raw_value = parse_json_value_token(text, cursor);
    if (!raw_value.has_value()) {
      return std::nullopt;
    }
    values[*key] = *raw_value;

    skip_ascii_ws(text, cursor);
    if (cursor < text.size() && text[cursor] == ',') {
      ++cursor;
      continue;
    }
    if (cursor < text.size() && text[cursor] == '}') {
      ++cursor;
      return values;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::string serialize_flat_json_object(
    const std::map<std::string, std::string>& values) {
  std::ostringstream stream;
  stream << '{';
  bool first = true;
  for (const auto& [key, value] : values) {
    if (!first) {
      stream << ',';
    }
    first = false;
    stream << '"' << escape_json(key) << '"' << ':' << value;
  }
  stream << '}';
  return stream.str();
}

[[nodiscard]] std::optional<std::string> extract_json_pointer_value(
    std::string_view payload,
    std::string_view pointer) {
  if (pointer.empty() || pointer.front() != '/') {
    return std::nullopt;
  }

  const auto values = parse_flat_json_object(payload);
  if (!values.has_value()) {
    return std::nullopt;
  }

  const std::string key(pointer.substr(1));
  const auto found = values->find(key);
  if (found == values->end()) {
    return std::nullopt;
  }

  return found->second;
}

[[nodiscard]] std::optional<std::string> inject_argument_value(
    std::string_view raw_arguments,
    const std::string& key,
    const std::string& raw_value) {
  const auto parsed = parse_flat_json_object(raw_arguments);
  if (!parsed.has_value()) {
    return std::nullopt;
  }

  auto updated = *parsed;
  updated[key] = raw_value;
  return serialize_flat_json_object(updated);
}

[[nodiscard]] std::string route_kind_string(ToolIRRoute route) {
  switch (route) {
    case ToolIRRoute::LocalTool:
      return "builtin";
    case ToolIRRoute::MCPRemote:
      return "mcp";
    case ToolIRRoute::WorkflowEngine:
      return "workflow";
    case ToolIRRoute::Unspecified:
      break;
  }
  return "workflow";
}

[[nodiscard]] ToolResult normalize_result(const WorkflowStep& step, ToolResult result) {
  if (!result.request_id.has_value()) {
    result.request_id = step.tool_ir.request_id;
  }
  if (!result.tool_call_id.has_value()) {
    result.tool_call_id = step.tool_ir.tool_call_id;
  }
  if (!result.tool_name.has_value()) {
    result.tool_name = step.tool_ir.tool_name;
  }
  if (!result.goal_id.has_value()) {
    result.goal_id = step.tool_ir.goal_id;
  }
  if (!result.worker_task_id.has_value()) {
    result.worker_task_id = step.tool_ir.worker_task_id;
  }
  if (!result.success.has_value()) {
    result.success = !result.error.has_value();
  }
  if (result.success.value_or(false)) {
    result.error = std::nullopt;
  } else if (!result.error.has_value()) {
    result.error = build_error(
        ResultCode::ToolExecutionFailed,
        "workflow.step_failed",
        "tools.workflow.dispatch",
        step.step_id);
  }
  if (!result.completed_at.has_value()) {
    result.completed_at = current_time_ms();
  }
  if (!result.duration_ms.has_value()) {
    result.duration_ms = 0;
  }
  return result;
}

[[nodiscard]] ToolResult build_failure_result(
    const WorkflowStep& step,
    ResultCode code,
    std::string message,
    std::string stage) {
  return ToolResult{
      .request_id = step.tool_ir.request_id,
      .tool_call_id = step.tool_ir.tool_call_id,
      .tool_name = step.tool_ir.tool_name,
      .success = false,
      .payload = std::nullopt,
      .error = build_error(code, std::move(message), std::move(stage), step.step_id),
      .side_effects = std::nullopt,
      .completed_at = current_time_ms(),
      .duration_ms = 0,
      .goal_id = step.tool_ir.goal_id,
      .worker_task_id = step.tool_ir.worker_task_id,
      .tags = std::vector<std::string>{"workflow"},
  };
}

[[nodiscard]] std::string serialize_string_array(const std::vector<std::string>& values) {
  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      stream << ',';
    }
    stream << '"' << escape_json(values[index]) << '"';
  }
  stream << ']';
  return stream.str();
}

[[nodiscard]] std::string serialize_receipt(
    const dasall::tools::execution::WorkflowReceipt& receipt) {
  std::ostringstream stream;
  stream << '{';
  stream << "\"workflow_id\":\"" << escape_json(receipt.workflow_id) << "\",";
  stream << "\"status\":\"" << escape_json(receipt.status) << "\",";
  stream << "\"completed_step_ids\":" << serialize_string_array(receipt.completed_step_ids)
         << ',';
  stream << "\"skipped_step_ids\":" << serialize_string_array(receipt.skipped_step_ids);
  if (receipt.failed_step_id.has_value()) {
    stream << ",\"failed_step_id\":\"" << escape_json(*receipt.failed_step_id) << "\"";
  }
  if (receipt.delegation_sidecar.has_value()) {
    stream << ",\"delegation_sidecar\":{";
    stream << "\"step_id\":\"" << escape_json(receipt.delegation_sidecar->step_id)
           << "\",";
    stream << "\"delegate_target\":\""
           << escape_json(receipt.delegation_sidecar->delegate_target) << "\",";
    stream << "\"reason_code\":\""
           << escape_json(receipt.delegation_sidecar->reason_code) << "\"";
    stream << '}';
  }
  stream << ",\"compensation_hint_count\":" << receipt.compensation_hints.size();
  stream << '}';
  return stream.str();
}

template <typename Value>
void append_unique(std::vector<Value>& target, const Value& value) {
  if (std::find(target.begin(), target.end(), value) == target.end()) {
    target.push_back(value);
  }
}

}  // namespace

namespace dasall::tools::execution {

WorkflowEngine::WorkflowEngine()
    : WorkflowEngine(default_dependencies()) {}

WorkflowEngine::WorkflowEngine(WorkflowEngineDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

WorkflowExecutionOutcome WorkflowEngine::execute(
    const ToolIR& workflow_ir,
    const ToolExecutionContext& execution_context) const {
  if (!dependencies_.plan_loader) {
    WorkflowReceipt receipt{
        .workflow_id = workflow_ir.tool_call_id.value_or(std::string("workflow.unconfigured")),
        .status = "rejected",
        .completed_step_ids = {},
        .skipped_step_ids = {},
        .failed_step_id = std::nullopt,
        .step_results = {},
        .compensation_hints = {},
        .evidence_refs = {"workflow.plan_loader_unavailable"},
        .failure_digest = build_error(
            ResultCode::ValidationFieldMissing,
            "workflow.plan_loader_unavailable",
            "tools.workflow.plan",
            workflow_ir.tool_call_id.value_or(std::string("workflow.unconfigured"))),
        .delegation_sidecar = std::nullopt,
    };
    return finalize_receipt(std::move(receipt), workflow_ir);
  }

  std::string failure_reason_code;
  std::string failure_message;
  const auto plan = dependencies_.plan_loader(workflow_ir, failure_reason_code, failure_message);
  if (!plan.has_value()) {
    WorkflowReceipt receipt{
        .workflow_id = workflow_ir.tool_call_id.value_or(std::string("workflow.invalid")),
        .status = "rejected",
        .completed_step_ids = {},
        .skipped_step_ids = {},
        .failed_step_id = std::nullopt,
        .step_results = {},
        .compensation_hints = {},
        .evidence_refs = {"workflow.plan_load_failed", failure_reason_code},
        .failure_digest = build_error(
            ResultCode::ValidationFieldMissing,
            failure_message.empty() ? failure_reason_code : failure_message,
            "tools.workflow.plan",
            workflow_ir.tool_call_id.value_or(std::string("workflow.invalid"))),
        .delegation_sidecar = std::nullopt,
    };
    return finalize_receipt(std::move(receipt), workflow_ir);
  }

  return execute(*plan, workflow_ir, execution_context);
}

WorkflowExecutionOutcome WorkflowEngine::execute(
    const WorkflowPlan& plan,
    const ToolIR& workflow_ir,
    const ToolExecutionContext& execution_context) const {
  auto receipt = WorkflowReceipt{
      .workflow_id = plan.workflow_id,
      .status = "completed",
      .completed_step_ids = {},
      .skipped_step_ids = {},
      .failed_step_id = std::nullopt,
      .step_results = {},
      .compensation_hints = {},
      .evidence_refs = {std::string("workflow:") + plan.workflow_id},
      .failure_digest = std::nullopt,
      .delegation_sidecar = std::nullopt,
  };

  const auto batch_result = build_batches(plan);
  if (!batch_result.ok) {
    receipt.status = "rejected";
    receipt.evidence_refs.insert(
        receipt.evidence_refs.end(),
        batch_result.evidence_refs.begin(),
        batch_result.evidence_refs.end());
    receipt.failure_digest = batch_result.error;
    return finalize_receipt(std::move(receipt), workflow_ir);
  }

  std::unordered_map<std::string, const WorkflowStep*> step_by_id;
  for (const auto& step : plan.steps) {
    step_by_id.emplace(step.step_id, &step);
  }

  std::uint32_t delegation_count = 0U;
  bool stop = false;
  for (const auto& batch : batch_result.batches) {
    for (const auto& step_id : batch.step_ids) {
      const auto found = step_by_id.find(step_id);
      if (found == step_by_id.end()) {
        continue;
      }

      auto dispatch_result = dispatch_step(
          plan,
          *found->second,
          execution_context,
          receipt.step_results,
          delegation_count);
      if (dispatch_result.delegation_sidecar.has_value()) {
        ++delegation_count;
      }
      collect_step_result(receipt, *found->second, std::move(dispatch_result));
      if (receipt.status == "failed") {
        stop = true;
        break;
      }
    }
    if (stop) {
      break;
    }
  }

  for (const auto& step : plan.steps) {
    const bool completed = std::find(
                               receipt.completed_step_ids.begin(),
                               receipt.completed_step_ids.end(),
                               step.step_id) != receipt.completed_step_ids.end();
    const bool failed = receipt.failed_step_id.has_value() &&
                        *receipt.failed_step_id == step.step_id;
    if (!completed && !failed) {
      receipt.skipped_step_ids.push_back(step.step_id);
    }
  }

  return finalize_receipt(std::move(receipt), workflow_ir);
}

WorkflowBatchBuildResult WorkflowEngine::build_batches(const WorkflowPlan& plan) const {
  if (plan.workflow_id.empty()) {
    return WorkflowBatchBuildResult{
        .ok = false,
        .batches = {},
        .error = build_error(
            ResultCode::ValidationFieldMissing,
            "workflow.plan_id_missing",
            "tools.workflow.build_batches",
            "workflow.plan"),
        .evidence_refs = {"workflow.plan_id_missing"},
        .failure_reason_code = "InvalidWorkflowPlan",
    };
  }

  if (plan.steps.empty() || plan.entry_step_ids.empty()) {
    return WorkflowBatchBuildResult{
        .ok = false,
        .batches = {},
        .error = build_error(
            ResultCode::ValidationFieldMissing,
            "workflow.plan_steps_missing",
            "tools.workflow.build_batches",
            plan.workflow_id),
        .evidence_refs = {"workflow.plan_steps_missing"},
        .failure_reason_code = "InvalidWorkflowPlan",
    };
  }

  std::unordered_map<std::string, std::size_t> step_order;
  std::unordered_map<std::string, std::set<std::string>> dependencies;
  for (std::size_t index = 0; index < plan.steps.size(); ++index) {
    const auto& step = plan.steps[index];
    if (step.step_id.empty() || step_order.contains(step.step_id)) {
      return WorkflowBatchBuildResult{
          .ok = false,
          .batches = {},
          .error = build_error(
              ResultCode::ValidationFieldMissing,
              "workflow.step_id_invalid",
              "tools.workflow.build_batches",
              plan.workflow_id),
          .evidence_refs = {"workflow.step_id_invalid"},
          .failure_reason_code = "InvalidWorkflowPlan",
      };
    }
    step_order.emplace(step.step_id, index);
    dependencies[step.step_id] = std::set<std::string>{};
  }

  for (const auto& entry_step_id : plan.entry_step_ids) {
    if (!step_order.contains(entry_step_id)) {
      return WorkflowBatchBuildResult{
          .ok = false,
          .batches = {},
          .error = build_error(
              ResultCode::ValidationFieldMissing,
              "workflow.entry_step_missing",
              "tools.workflow.build_batches",
              entry_step_id),
          .evidence_refs = {std::string("entry:") + entry_step_id},
          .failure_reason_code = "InvalidWorkflowPlan",
      };
    }
  }

  for (const auto& step : plan.steps) {
    for (const auto& dependency : step.depends_on) {
      if (!step_order.contains(dependency) || dependency == step.step_id) {
        return WorkflowBatchBuildResult{
            .ok = false,
            .batches = {},
            .error = build_error(
                ResultCode::ValidationFieldMissing,
                "workflow.depends_on_invalid",
                "tools.workflow.build_batches",
                step.step_id),
            .evidence_refs = {std::string("step:") + step.step_id},
            .failure_reason_code = "InvalidWorkflowPlan",
        };
      }
      dependencies[step.step_id].insert(dependency);
    }
  }

  for (const auto& edge : plan.edges) {
    if (!step_order.contains(edge.from_step_id) || !step_order.contains(edge.to_step_id) ||
        edge.from_step_id == edge.to_step_id) {
      return WorkflowBatchBuildResult{
          .ok = false,
          .batches = {},
          .error = build_error(
              ResultCode::ValidationFieldMissing,
              "workflow.edge_invalid",
              "tools.workflow.build_batches",
              plan.workflow_id),
          .evidence_refs = {std::string("edge:") + edge.from_step_id + "->" + edge.to_step_id},
          .failure_reason_code = "InvalidWorkflowPlan",
      };
    }
    dependencies[edge.to_step_id].insert(edge.from_step_id);
  }

  for (const auto& binding : plan.step_output_mapping) {
    if (!step_order.contains(binding.source_step_id) ||
        !step_order.contains(binding.target_step_id) ||
        binding.source_json_pointer.empty() || binding.target_argument_key.empty()) {
      return WorkflowBatchBuildResult{
          .ok = false,
          .batches = {},
          .error = build_error(
              ResultCode::ValidationFieldMissing,
              "workflow.output_mapping_invalid",
              "tools.workflow.build_batches",
              plan.workflow_id),
          .evidence_refs = {std::string("binding:") + binding.source_step_id + "->" +
                            binding.target_step_id},
          .failure_reason_code = "InvalidWorkflowPlan",
      };
    }
    dependencies[binding.target_step_id].insert(binding.source_step_id);
  }

  std::unordered_map<std::string, std::vector<std::string>> adjacency;
  std::unordered_map<std::string, std::size_t> indegree;
  for (const auto& [step_id, step_dependencies] : dependencies) {
    indegree[step_id] = step_dependencies.size();
    for (const auto& dependency : step_dependencies) {
      adjacency[dependency].push_back(step_id);
    }
  }

  std::vector<std::string> zero_indegree;
  for (const auto& step : plan.steps) {
    if (indegree[step.step_id] == 0U) {
      zero_indegree.push_back(step.step_id);
    }
  }
  std::sort(zero_indegree.begin(), zero_indegree.end(), [&](const auto& left, const auto& right) {
    return step_order[left] < step_order[right];
  });

  for (const auto& step_id : zero_indegree) {
    if (std::find(plan.entry_step_ids.begin(), plan.entry_step_ids.end(), step_id) ==
        plan.entry_step_ids.end()) {
      return WorkflowBatchBuildResult{
          .ok = false,
          .batches = {},
          .error = build_error(
              ResultCode::ValidationFieldMissing,
              "workflow.entry_step_incomplete",
              "tools.workflow.build_batches",
              plan.workflow_id),
          .evidence_refs = {std::string("entry:") + step_id},
          .failure_reason_code = "InvalidWorkflowPlan",
      };
    }
  }

  std::vector<WorkflowBatch> batches;
  std::size_t processed = 0U;
  auto ready = zero_indegree;
  while (!ready.empty()) {
    WorkflowBatch batch;
    batch.step_ids = ready;
    batches.push_back(batch);
    processed += ready.size();

    std::vector<std::string> next;
    for (const auto& step_id : ready) {
      for (const auto& dependent : adjacency[step_id]) {
        if (indegree[dependent] > 0U) {
          --indegree[dependent];
          if (indegree[dependent] == 0U) {
            next.push_back(dependent);
          }
        }
      }
    }
    std::sort(next.begin(), next.end(), [&](const auto& left, const auto& right) {
      return step_order[left] < step_order[right];
    });
    ready = std::move(next);
  }

  if (processed != plan.steps.size()) {
    return WorkflowBatchBuildResult{
        .ok = false,
        .batches = {},
        .error = build_error(
            ResultCode::ValidationFieldMissing,
            "workflow.cyclic_graph",
            "tools.workflow.build_batches",
            plan.workflow_id),
        .evidence_refs = {std::string("workflow:") + plan.workflow_id, "workflow.cyclic_graph"},
        .failure_reason_code = "InvalidWorkflowPlan",
    };
  }

  return WorkflowBatchBuildResult{
      .ok = true,
      .batches = std::move(batches),
      .error = std::nullopt,
      .evidence_refs = {},
      .failure_reason_code = {},
  };
}

WorkflowStepDispatchResult WorkflowEngine::dispatch_step(
    const WorkflowPlan& plan,
    const WorkflowStep& step,
    const ToolExecutionContext& execution_context,
    const std::vector<WorkflowStepReceipt>& prior_results,
    std::uint32_t delegation_count) const {
  if (step.step_kind == WorkflowStepKind::Delegation) {
    if (plan.delegation_policy.mode != WorkflowDelegationMode::RecommendOnly) {
      return WorkflowStepDispatchResult{
          .ok = false,
          .step_id = step.step_id,
          .route_kind = "workflow",
          .result = build_failure_result(
              step,
              ResultCode::PolicyDenied,
              "workflow.delegation_disabled",
              "tools.workflow.delegation"),
          .mapped_arguments = {},
          .evidence_refs = {std::string("workflow:") + step.step_id},
          .delegation_sidecar = std::nullopt,
          .failure_reason_code = "workflow.delegation_disabled",
      };
    }
    if (plan.delegation_policy.max_delegate_steps > 0U &&
        delegation_count >= plan.delegation_policy.max_delegate_steps) {
      return WorkflowStepDispatchResult{
          .ok = false,
          .step_id = step.step_id,
          .route_kind = "workflow",
          .result = build_failure_result(
              step,
              ResultCode::PolicyDenied,
              "workflow.delegation_limit_exceeded",
              "tools.workflow.delegation"),
          .mapped_arguments = {},
          .evidence_refs = {std::string("workflow:") + step.step_id},
          .delegation_sidecar = std::nullopt,
          .failure_reason_code = "workflow.delegation_limit_exceeded",
      };
    }

    const auto delegate_target = step.delegate_target.value_or(std::string("runtime.delegate"));
    return WorkflowStepDispatchResult{
        .ok = true,
        .step_id = step.step_id,
        .route_kind = "workflow",
        .result = ToolResult{
            .request_id = step.tool_ir.request_id,
            .tool_call_id = step.tool_ir.tool_call_id,
            .tool_name = step.tool_ir.tool_name,
            .success = true,
            .payload = std::string("{\"delegation\":\"recommended\",\"delegate_target\":\"") +
                       escape_json(delegate_target) + "\"}",
            .error = std::nullopt,
            .side_effects = std::nullopt,
            .completed_at = current_time_ms(),
            .duration_ms = 0,
            .goal_id = step.tool_ir.goal_id,
            .worker_task_id = step.tool_ir.worker_task_id,
            .tags = std::vector<std::string>{"workflow", "delegation"},
        },
        .mapped_arguments = {},
        .evidence_refs = {std::string("workflow:") + step.step_id,
                          std::string("delegate_target:") + delegate_target},
        .delegation_sidecar = WorkflowDelegationSidecar{
            .step_id = step.step_id,
            .delegate_target = delegate_target,
            .reason_code = "workflow.delegation.recommend_only",
            .input_evidence_refs = {std::string("workflow:") + step.step_id},
        },
        .failure_reason_code = {},
    };
  }

  auto prepared_ir = step.tool_ir;
  if (step.timeout_override_ms.has_value()) {
    prepared_ir.timeout_ms = step.timeout_override_ms;
  }

  std::vector<std::string> mapped_arguments;
  for (const auto& binding : plan.step_output_mapping) {
    if (binding.target_step_id != step.step_id) {
      continue;
    }

    const auto source_receipt = std::find_if(
        prior_results.begin(), prior_results.end(), [&](const WorkflowStepReceipt& receipt) {
          return receipt.step_id == binding.source_step_id;
        });
    if (source_receipt == prior_results.end() ||
        !source_receipt->result.success.value_or(false) ||
        !source_receipt->result.payload.has_value()) {
      if (!binding.required) {
        continue;
      }
      return WorkflowStepDispatchResult{
          .ok = false,
          .step_id = step.step_id,
          .route_kind = "workflow",
          .result = build_failure_result(
              step,
              ResultCode::ValidationFieldMissing,
              "workflow.mapping_source_missing",
              "tools.workflow.mapping"),
          .mapped_arguments = mapped_arguments,
          .evidence_refs = {std::string("binding:") + binding.source_step_id + "->" +
                            binding.target_step_id},
          .delegation_sidecar = std::nullopt,
          .failure_reason_code = "workflow.mapping_source_missing",
      };
    }

    const auto raw_value = extract_json_pointer_value(
        *source_receipt->result.payload,
        binding.source_json_pointer);
    if (!raw_value.has_value()) {
      if (!binding.required) {
        continue;
      }
      return WorkflowStepDispatchResult{
          .ok = false,
          .step_id = step.step_id,
          .route_kind = "workflow",
          .result = build_failure_result(
              step,
              ResultCode::ValidationFieldMissing,
              "workflow.mapping_pointer_missing",
              "tools.workflow.mapping"),
          .mapped_arguments = mapped_arguments,
          .evidence_refs = {std::string("binding:") + binding.source_json_pointer},
          .delegation_sidecar = std::nullopt,
          .failure_reason_code = "workflow.mapping_pointer_missing",
      };
    }

    if (!prepared_ir.normalized_arguments.has_value()) {
      prepared_ir.normalized_arguments = std::string("{}");
    }
    const auto injected_arguments = inject_argument_value(
        *prepared_ir.normalized_arguments,
        binding.target_argument_key,
        *raw_value);
    if (!injected_arguments.has_value()) {
      return WorkflowStepDispatchResult{
          .ok = false,
          .step_id = step.step_id,
          .route_kind = "workflow",
          .result = build_failure_result(
              step,
              ResultCode::ValidationFieldMissing,
              "workflow.mapping_target_invalid",
              "tools.workflow.mapping"),
          .mapped_arguments = mapped_arguments,
          .evidence_refs = {std::string("target:") + binding.target_argument_key},
          .delegation_sidecar = std::nullopt,
          .failure_reason_code = "workflow.mapping_target_invalid",
      };
    }
    prepared_ir.normalized_arguments = *injected_arguments;
    mapped_arguments.push_back(binding.target_argument_key);
  }

  const auto step_route = prepared_ir.route.has_value()
                              ? *prepared_ir.route
                              : step.route_kind_hint.value_or(ToolIRRoute::LocalTool);
  WorkflowStepExecutor executor;
  if (step_route == ToolIRRoute::LocalTool) {
    executor = dependencies_.builtin_executor;
  } else if (step_route == ToolIRRoute::MCPRemote) {
    executor = dependencies_.mcp_executor;
  }

  if (step_route == ToolIRRoute::WorkflowEngine || !executor) {
    return WorkflowStepDispatchResult{
        .ok = false,
        .step_id = step.step_id,
        .route_kind = route_kind_string(step_route),
        .result = build_failure_result(
            step,
            ResultCode::ToolExecutionFailed,
            "workflow.executor_unavailable",
            "tools.workflow.dispatch"),
        .mapped_arguments = mapped_arguments,
        .evidence_refs = {std::string("workflow:") + step.step_id},
        .delegation_sidecar = std::nullopt,
        .failure_reason_code = "workflow.executor_unavailable",
    };
  }

  prepared_ir.route = step_route;
  auto result = normalize_result(step, executor(prepared_ir, execution_context));
  std::vector<std::string> evidence_refs = {std::string("workflow:") + step.step_id};
  if (result.tool_call_id.has_value()) {
    evidence_refs.push_back(std::string("tool_call:") + *result.tool_call_id);
  }
  if (result.side_effects.has_value()) {
    evidence_refs.insert(
        evidence_refs.end(), result.side_effects->begin(), result.side_effects->end());
  }

  return WorkflowStepDispatchResult{
      .ok = result.success.value_or(false),
      .step_id = step.step_id,
      .route_kind = route_kind_string(step_route),
      .result = std::move(result),
      .mapped_arguments = std::move(mapped_arguments),
      .evidence_refs = std::move(evidence_refs),
      .delegation_sidecar = std::nullopt,
      .failure_reason_code = result.success.value_or(false) ? std::string() : "workflow.step_failed",
  };
}

void WorkflowEngine::collect_step_result(
    WorkflowReceipt& receipt,
    const WorkflowStep& step,
    WorkflowStepDispatchResult dispatch_result) const {
  WorkflowStepReceipt step_receipt{
      .step_id = step.step_id,
      .route_kind = dispatch_result.route_kind,
      .result = dispatch_result.result,
      .mapped_arguments = std::move(dispatch_result.mapped_arguments),
      .evidence_refs = dispatch_result.evidence_refs,
  };
  receipt.step_results.push_back(std::move(step_receipt));

  for (const auto& evidence_ref : dispatch_result.evidence_refs) {
    append_unique(receipt.evidence_refs, evidence_ref);
  }

  if (dispatch_result.delegation_sidecar.has_value() &&
      !receipt.delegation_sidecar.has_value()) {
    receipt.delegation_sidecar = dispatch_result.delegation_sidecar;
  }

  if (dispatch_result.result.success.value_or(false)) {
    receipt.completed_step_ids.push_back(step.step_id);
    return;
  }

  receipt.status = "failed";
  receipt.failed_step_id = step.step_id;
  receipt.failure_digest = dispatch_result.result.error.has_value()
                              ? dispatch_result.result.error
                              : std::optional<ErrorInfo>(build_error(
                                    ResultCode::ToolExecutionFailed,
                                    dispatch_result.failure_reason_code.empty()
                                        ? std::string("workflow.step_failed")
                                        : dispatch_result.failure_reason_code,
                                    "tools.workflow.collect",
                                    step.step_id));
}

WorkflowExecutionOutcome WorkflowEngine::finalize_receipt(
    WorkflowReceipt receipt,
    const ToolIR& workflow_ir) const {
  std::vector<std::string> aggregated_side_effects;
  for (const auto& step_receipt : receipt.step_results) {
    if (!step_receipt.result.side_effects.has_value()) {
      continue;
    }
    for (const auto& side_effect : *step_receipt.result.side_effects) {
      append_unique(aggregated_side_effects, side_effect);
    }
  }

  const bool success = receipt.status == "completed";
  const auto payload = serialize_receipt(receipt);
  auto tool_result = ToolResult{
      .request_id = workflow_ir.request_id,
      .tool_call_id = workflow_ir.tool_call_id,
      .tool_name = workflow_ir.tool_name,
      .success = success,
      .payload = payload,
      .error = success ? std::nullopt : receipt.failure_digest,
      .side_effects = aggregated_side_effects.empty()
                          ? std::nullopt
                          : std::optional<std::vector<std::string>>(aggregated_side_effects),
      .completed_at = current_time_ms(),
      .duration_ms = 0,
      .goal_id = workflow_ir.goal_id,
      .worker_task_id = workflow_ir.worker_task_id,
      .tags = std::vector<std::string>{"workflow"},
  };

  return WorkflowExecutionOutcome{
      .receipt = std::move(receipt),
      .tool_result = std::move(tool_result),
      .compensation_hints = std::nullopt,
      .evidence_refs = {},
      .failure_reason_code = success ? std::string() :
          (tool_result.error.has_value() && !tool_result.error->details.message.empty()
               ? tool_result.error->details.message
               : std::string("workflow.step_failed")),
  };
}

WorkflowEngineDependencies WorkflowEngine::default_dependencies() {
  return WorkflowEngineDependencies{
      .plan_loader = {},
      .builtin_executor = {},
      .mcp_executor = {},
  };
}

}  // namespace dasall::tools::execution
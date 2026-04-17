#include "skills/SkillRuntime.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "tool/ToolIR.h"

namespace dasall::tools::skills {

namespace {

struct ParsedKeyValueYaml {
  std::unordered_map<std::string, std::string> scalar_values;
  std::unordered_map<std::string, std::vector<std::string>> list_values;
  bool ok = false;
  std::string error;
};

[[nodiscard]] std::string trim_copy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }

  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1U);
}

[[nodiscard]] std::size_t count_indent(const std::string& line) {
  std::size_t indent = 0U;
  while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) {
    ++indent;
  }

  return indent;
}

[[nodiscard]] std::string strip_optional_quotes(std::string value) {
  if (value.size() >= 2U &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1U, value.size() - 2U);
  }

  return value;
}

[[nodiscard]] std::string strip_inline_comment(std::string value) {
  const auto comment_pos = value.find('#');
  if (comment_pos == std::string::npos) {
    return strip_optional_quotes(trim_copy(std::move(value)));
  }

  return strip_optional_quotes(trim_copy(value.substr(0U, comment_pos)));
}

[[nodiscard]] std::string join_path(
    const std::vector<std::pair<std::size_t, std::string>>& path,
    const std::string& leaf_key) {
  std::ostringstream stream;
  bool first = true;

  for (const auto& node : path) {
    if (!first) {
      stream << '.';
    }
    stream << node.second;
    first = false;
  }

  if (!leaf_key.empty()) {
    if (!first) {
      stream << '.';
    }
    stream << leaf_key;
  }

  return stream.str();
}

[[nodiscard]] ParsedKeyValueYaml parse_key_value_yaml_file(
    const std::filesystem::path& yaml_path) {
  ParsedKeyValueYaml parsed;

  std::ifstream stream(yaml_path);
  if (!stream.is_open()) {
    parsed.error = "unable to open yaml file";
    return parsed;
  }

  std::vector<std::pair<std::size_t, std::string>> path_stack;
  std::string raw_line;
  while (std::getline(stream, raw_line)) {
    const std::string trimmed = trim_copy(raw_line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
      continue;
    }

    const std::size_t indent = count_indent(raw_line);
    if (trimmed.starts_with("- ")) {
      while (!path_stack.empty() && path_stack.back().first >= indent) {
        path_stack.pop_back();
      }

      if (path_stack.empty()) {
        parsed.error = "yaml list item is missing parent key";
        return parsed;
      }

      parsed.list_values[join_path(path_stack, "")].push_back(
          strip_inline_comment(trimmed.substr(2U)));
      continue;
    }

    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      parsed.error = "yaml line missing colon separator";
      return parsed;
    }

    const std::string key = trim_copy(trimmed.substr(0U, colon));
    const std::string value = strip_inline_comment(trimmed.substr(colon + 1U));

    while (!path_stack.empty() && path_stack.back().first >= indent) {
      path_stack.pop_back();
    }

    if (value.empty()) {
      path_stack.emplace_back(indent, key);
      continue;
    }

    parsed.scalar_values[join_path(path_stack, key)] = value;
  }

  parsed.ok = true;
  return parsed;
}

[[nodiscard]] bool contains_value(
    const std::vector<std::string>& values,
    std::string_view candidate) {
  return std::find(values.begin(), values.end(), candidate) != values.end();
}

[[nodiscard]] std::string tool_domain(std::string_view tool_name) {
  const auto delimiter = tool_name.find('.');
  if (delimiter == std::string::npos) {
    return std::string(tool_name);
  }

  return std::string(tool_name.substr(0U, delimiter));
}

[[nodiscard]] std::optional<std::uint32_t> parse_uint32(
    const std::unordered_map<std::string, std::string>& scalars,
    const std::string& key) {
  const auto found = scalars.find(key);
  if (found == scalars.end()) {
    return std::nullopt;
  }

  try {
    const auto parsed = std::stoul(found->second);
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<dasall::contracts::ToolIRRoute> parse_route_kind(
    std::string_view value) {
  if (value == "LocalTool") {
    return dasall::contracts::ToolIRRoute::LocalTool;
  }
  if (value == "WorkflowEngine") {
    return dasall::contracts::ToolIRRoute::WorkflowEngine;
  }
  if (value == "MCPRemote") {
    return dasall::contracts::ToolIRRoute::MCPRemote;
  }

  return std::nullopt;
}

[[nodiscard]] dasall::tools::execution::WorkflowDelegationMode parse_delegation_mode(
    std::string_view value) {
  if (value == "RecommendOnly") {
    return dasall::tools::execution::WorkflowDelegationMode::RecommendOnly;
  }

  return dasall::tools::execution::WorkflowDelegationMode::Disabled;
}

}  // namespace

SkillRuntime::SkillRuntime()
    : SkillRuntime(default_project_root()) {}

SkillRuntime::SkillRuntime(std::filesystem::path project_root)
    : project_root_(std::move(project_root)) {}

SkillInstantiateResult SkillRuntime::instantiate(
    const SkillMatchResult& match_result,
    const ToolPolicyView& policy_view) {
  if (!match_result.matched || !match_result.asset.has_value()) {
    return SkillInstantiateResult{
        .instantiated = false,
        .instance = std::nullopt,
        .workflow_plan = std::nullopt,
        .reason_code = "skill.runtime.match_missing",
        .denied_tools = {},
        .fallback_strategy = std::nullopt,
    };
  }

  const auto& asset = *match_result.asset;
  if (!asset.has_consistent_values()) {
    return SkillInstantiateResult{
        .instantiated = false,
        .instance = std::nullopt,
        .workflow_plan = std::nullopt,
        .reason_code = "skill.runtime.asset_invalid",
        .denied_tools = {},
        .fallback_strategy = asset.fallback_mode,
    };
  }

  const auto tool_allowlist = build_tool_allowlist(asset, policy_view);
  std::vector<std::string> denied_tools;
  for (const auto& tool_name : asset.allowed_tools) {
    if (!contains_value(tool_allowlist, tool_name)) {
      denied_tools.push_back(tool_name);
    }
  }

  if (!denied_tools.empty()) {
    return SkillInstantiateResult{
        .instantiated = false,
        .instance = std::nullopt,
        .workflow_plan = std::nullopt,
        .reason_code = "skill.runtime.policy_denied",
        .denied_tools = std::move(denied_tools),
        .fallback_strategy = asset.fallback_mode,
    };
  }

  SkillInstance instance{
      .instance_id = allocate_instance_id(),
      .asset = asset,
      .tool_allowlist = tool_allowlist,
      .workflow_template_ref = asset.workflow_template_ref,
      .prompt_bundle_ref = asset.prompt_bundle_ref,
      .eval_suite_ref = asset.eval_suite_ref,
      .fallback_strategy = asset.fallback_mode,
  };

  std::string reason_code;
  auto workflow_plan = bind_workflow_template(instance, reason_code);
  if (!workflow_plan.has_value()) {
    return SkillInstantiateResult{
        .instantiated = false,
        .instance = std::nullopt,
        .workflow_plan = std::nullopt,
        .reason_code = reason_code,
        .denied_tools = {},
        .fallback_strategy = asset.fallback_mode,
    };
  }

  {
    std::lock_guard<std::mutex> guard(instance_mutex_);
    active_instances_[instance.instance_id] = instance;
  }

  return SkillInstantiateResult{
      .instantiated = true,
      .instance = instance,
      .workflow_plan = std::move(workflow_plan),
      .reason_code = "skill.runtime.instantiated",
      .denied_tools = {},
      .fallback_strategy = asset.fallback_mode,
  };
}

std::optional<execution::WorkflowPlan> SkillRuntime::bind_workflow_template(
    const SkillInstance& instance,
    std::string& reason_code) const {
  if (instance.workflow_template_ref.empty()) {
    reason_code = "skill.runtime.workflow_missing";
    return std::nullopt;
  }

  const auto workflow_path = resolve_asset_path(instance.workflow_template_ref);
  const auto parsed = parse_key_value_yaml_file(workflow_path);
  if (!parsed.ok) {
    reason_code = "skill.runtime.workflow_parse_failed";
    return std::nullopt;
  }

  const auto workflow_id_it = parsed.scalar_values.find("workflow_id");
  const auto entry_steps_it = parsed.list_values.find("entry_step_ids");
  const auto steps_it = parsed.list_values.find("steps");
  if (workflow_id_it == parsed.scalar_values.end() ||
      entry_steps_it == parsed.list_values.end() ||
      steps_it == parsed.list_values.end() || steps_it->second.empty()) {
    reason_code = "skill.runtime.workflow_invalid";
    return std::nullopt;
  }

  execution::WorkflowPlan plan{
      .workflow_id = workflow_id_it->second,
      .entry_step_ids = entry_steps_it->second,
      .steps = {},
      .edges = {},
      .step_output_mapping = {},
      .delegation_policy = execution::WorkflowDelegationPolicy{
          .mode = parse_delegation_mode(
              parsed.scalar_values.contains("delegation_mode")
                  ? parsed.scalar_values.at("delegation_mode")
                  : std::string("Disabled")),
          .max_delegate_steps = parse_uint32(parsed.scalar_values, "max_delegate_steps")
                                    .value_or(0U),
      },
      .metadata = {
          {"skill_id", instance.asset.skill_id},
          {"instance_id", instance.instance_id},
      },
  };

  for (const auto& step_id : steps_it->second) {
    const std::string prefix = std::string("step.") + step_id + ".";
    const auto tool_name_it = parsed.scalar_values.find(prefix + "tool_name");
    const auto route_kind_it = parsed.scalar_values.find(prefix + "route_kind");
    if (tool_name_it == parsed.scalar_values.end() ||
        route_kind_it == parsed.scalar_values.end()) {
      reason_code = "skill.runtime.workflow_invalid";
      return std::nullopt;
    }

    if (!contains_value(instance.tool_allowlist, tool_name_it->second)) {
      reason_code = "skill.runtime.tool_not_allowed";
      return std::nullopt;
    }

    const auto route_kind = parse_route_kind(route_kind_it->second);
    if (!route_kind.has_value()) {
      reason_code = "skill.runtime.workflow_invalid";
      return std::nullopt;
    }

    plan.steps.push_back(execution::WorkflowStep{
        .step_id = step_id,
        .tool_ir = contracts::ToolIR{
            .request_id = instance.instance_id,
            .tool_call_id = instance.instance_id + ":" + step_id,
            .tool_name = tool_name_it->second,
            .operation = contracts::ToolIROperation::Invoke,
            .normalized_arguments = std::string("{}"),
            .route = *route_kind,
            .timeout_ms = parse_uint32(parsed.scalar_values, prefix + "timeout_ms"),
            .idempotency_key = instance.instance_id + ":" + tool_name_it->second,
            .priority = contracts::ToolIRPriority::Normal,
            .goal_id = instance.instance_id,
            .worker_task_id = instance.instance_id,
        },
        .route_kind_hint = *route_kind,
        .step_kind = execution::WorkflowStepKind::Tool,
        .depends_on = parsed.list_values.contains(prefix + "depends_on")
                          ? parsed.list_values.at(prefix + "depends_on")
                          : std::vector<std::string>{},
        .allow_partial_side_effect = false,
        .timeout_override_ms = parse_uint32(parsed.scalar_values, prefix + "timeout_ms"),
        .delegate_target = std::nullopt,
    });
  }

  std::map<std::string, execution::WorkflowStepOutputBinding> bindings_by_key;
  for (const auto& [key, value] : parsed.scalar_values) {
    if (!key.starts_with("binding.")) {
      continue;
    }

    const auto first_dot = key.find('.', 8U);
    const auto last_dot = key.rfind('.');
    if (first_dot == std::string::npos || last_dot == std::string::npos ||
        first_dot == last_dot) {
      reason_code = "skill.runtime.workflow_invalid";
      return std::nullopt;
    }

    const auto source_step_id = key.substr(8U, first_dot - 8U);
    const auto binding_name = key.substr(first_dot + 1U, last_dot - first_dot - 1U);
    const auto field_name = key.substr(last_dot + 1U);
    const auto binding_key = source_step_id + ":" + binding_name;
    auto& binding = bindings_by_key[binding_key];
    binding.source_step_id = source_step_id;
    binding.source_json_pointer = std::string("/") + binding_name;

    if (field_name == "target_step_id") {
      binding.target_step_id = value;
    } else if (field_name == "target_argument_key") {
      binding.target_argument_key = value;
    }
  }

  for (const auto& [binding_key, binding] : bindings_by_key) {
    static_cast<void>(binding_key);
    if (binding.source_step_id.empty() || binding.target_step_id.empty() ||
        binding.target_argument_key.empty()) {
      reason_code = "skill.runtime.workflow_invalid";
      return std::nullopt;
    }
    plan.step_output_mapping.push_back(binding);
  }

  reason_code = "skill.runtime.workflow_bound";
  return plan;
}

std::vector<std::string> SkillRuntime::build_tool_allowlist(
    const SkillSpecAsset& asset,
    const ToolPolicyView& policy_view) const {
  if (policy_view.effective_profile_id.empty()) {
    return {};
  }

  if (!asset.profile_constraints.empty() &&
      !contains_value(asset.profile_constraints, policy_view.effective_profile_id)) {
    return {};
  }

  const bool all_domains_allowed = contains_value(policy_view.allowed_tool_domains, "all");
  for (const auto& required_domain : asset.required_domains) {
    if (!all_domains_allowed &&
        !contains_value(policy_view.allowed_tool_domains, required_domain)) {
      return {};
    }
  }

  std::vector<std::string> tool_allowlist;
  for (const auto& tool_name : asset.allowed_tools) {
    const auto domain = tool_domain(tool_name);
    if (all_domains_allowed || contains_value(policy_view.allowed_tool_domains, domain)) {
      tool_allowlist.push_back(tool_name);
    }
  }

  return tool_allowlist;
}

bool SkillRuntime::release_instance(std::string_view instance_id) {
  if (instance_id.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> guard(instance_mutex_);
  return active_instances_.erase(std::string(instance_id)) > 0U;
}

std::filesystem::path SkillRuntime::default_project_root() {
  auto project_root = std::filesystem::path(__FILE__);
  for (int level = 0; level < 4; ++level) {
    project_root = project_root.parent_path();
  }

  return project_root;
}

std::filesystem::path SkillRuntime::resolve_asset_path(std::string_view asset_ref) const {
  const std::filesystem::path candidate(asset_ref);
  if (candidate.is_absolute()) {
    return candidate;
  }

  return (project_root_ / candidate).lexically_normal();
}

std::string SkillRuntime::allocate_instance_id() {
  std::lock_guard<std::mutex> guard(instance_mutex_);
  return std::string("skill-instance-") + std::to_string(next_instance_id_++);
}

}  // namespace dasall::tools::skills
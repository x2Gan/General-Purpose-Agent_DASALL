#include "ProfileOverlayComposer.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace dasall::profiles {
namespace {

struct MutableRuntimePolicySnapshot {
  std::uint64_t generation;
  std::string effective_profile_id;
  contracts::RuntimeBudget runtime_budget;
  ModelProfile model_profile;
  TokenBudgetPolicy token_budget_policy;
  PromptPolicy prompt_policy;
  CapabilityCachePolicy capability_cache_policy;
  DegradePolicy degrade_policy;
  TimeoutPolicy timeout_policy;
  ExecutionPolicy execution_policy;
  OpsPolicy ops_policy;
  std::uint32_t worker_threads;
  bool multi_agent_enabled;
  MemoryMaintenancePolicy memory_maintenance_policy;
};

[[nodiscard]] MutableRuntimePolicySnapshot make_mutable_snapshot(const RuntimePolicySnapshot& snapshot) {
  return MutableRuntimePolicySnapshot{
      .generation = snapshot.generation(),
      .effective_profile_id = snapshot.effective_profile_id(),
      .runtime_budget = snapshot.runtime_budget(),
      .model_profile = snapshot.model_profile(),
      .token_budget_policy = snapshot.token_budget_policy(),
      .prompt_policy = snapshot.prompt_policy(),
      .capability_cache_policy = snapshot.capability_cache_policy(),
      .degrade_policy = snapshot.degrade_policy(),
      .timeout_policy = snapshot.timeout_policy(),
      .execution_policy = snapshot.execution_policy(),
      .ops_policy = snapshot.ops_policy(),
      .worker_threads = snapshot.worker_threads(),
      .multi_agent_enabled = snapshot.multi_agent_enabled(),
      .memory_maintenance_policy = snapshot.memory_maintenance_policy(),
  };
}

[[nodiscard]] RuntimePolicySnapshot freeze_snapshot(const MutableRuntimePolicySnapshot& snapshot) {
  return RuntimePolicySnapshot{
      snapshot.generation,
      snapshot.effective_profile_id,
      snapshot.runtime_budget,
      snapshot.model_profile,
      snapshot.token_budget_policy,
      snapshot.prompt_policy,
      snapshot.capability_cache_policy,
      snapshot.degrade_policy,
      snapshot.timeout_policy,
      snapshot.execution_policy,
      snapshot.ops_policy,
      snapshot.worker_threads,
      snapshot.multi_agent_enabled,
      snapshot.memory_maintenance_policy,
  };
}

[[nodiscard]] bool is_deployment_source(ProfileOverrideSourceKind source_kind) {
  switch (source_kind) {
    case ProfileOverrideSourceKind::DeploymentBundle:
    case ProfileOverrideSourceKind::SiteBundle:
    case ProfileOverrideSourceKind::DeviceBundle:
    case ProfileOverrideSourceKind::ExternalStoreSnapshot:
      return true;
    case ProfileOverrideSourceKind::RuntimeCommand:
    case ProfileOverrideSourceKind::DiagnosticSession:
      return false;
  }

  return false;
}

[[nodiscard]] bool is_runtime_source(ProfileOverrideSourceKind source_kind) {
  switch (source_kind) {
    case ProfileOverrideSourceKind::RuntimeCommand:
    case ProfileOverrideSourceKind::DiagnosticSession:
      return true;
    case ProfileOverrideSourceKind::DeploymentBundle:
    case ProfileOverrideSourceKind::SiteBundle:
    case ProfileOverrideSourceKind::DeviceBundle:
    case ProfileOverrideSourceKind::ExternalStoreSnapshot:
      return false;
  }

  return false;
}

[[nodiscard]] bool is_runtime_scope(ProfileOverrideTargetScope scope) {
  return scope == ProfileOverrideTargetScope::Device || scope == ProfileOverrideTargetScope::Process;
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
  return value.starts_with(prefix);
}

[[nodiscard]] bool is_forbidden_path(std::string_view path) {
  return path == "schema_version" || starts_with(path, "profile_meta.") ||
         starts_with(path, "enabled_modules.") || starts_with(path, "execution_policy.");
}

[[nodiscard]] bool is_deployment_path_allowed(std::string_view path) {
  return starts_with(path, "runtime_budget.") || starts_with(path, "timeout_policy.") ||
         starts_with(path, "ops_policy.") || starts_with(path, "capability_cache_policy.") ||
         starts_with(path, "degrade_policy.") ||
         (starts_with(path, "model_profile.") && path.ends_with(".fallback_route"));
}

[[nodiscard]] bool is_runtime_path_allowed(std::string_view path) {
  if (starts_with(path, "runtime_budget.") || starts_with(path, "timeout_policy.") ||
      starts_with(path, "capability_cache_policy.")) {
    return true;
  }

  return path == "ops_policy.log_level" || path == "ops_policy.trace_sample_ratio" ||
         path == "ops_policy.remote_diagnostics_enabled";
}

[[nodiscard]] std::optional<std::uint32_t> parse_uint32(std::string_view value) {
  std::uint32_t parsed_value = 0U;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed_value);
  if (error != std::errc() || ptr != end) {
    return std::nullopt;
  }

  return parsed_value;
}

[[nodiscard]] std::optional<std::int64_t> parse_int64(std::string_view value) {
  std::int64_t parsed_value = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto [ptr, error] = std::from_chars(begin, end, parsed_value);
  if (error != std::errc() || ptr != end) {
    return std::nullopt;
  }

  return parsed_value;
}

[[nodiscard]] std::optional<double> parse_double(std::string_view value) {
  std::stringstream stream;
  stream << value;

  double parsed_value = 0.0;
  stream >> parsed_value;
  if (!stream || !stream.eof()) {
    return std::nullopt;
  }

  return parsed_value;
}

[[nodiscard]] std::optional<bool> parse_bool(std::string_view value) {
  if (value == "true") {
    return true;
  }

  if (value == "false") {
    return false;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<std::string>> parse_csv_list(std::string_view value) {
  std::vector<std::string> items;
  std::stringstream stream{std::string(value)};
  std::string item;
  while (std::getline(stream, item, ',')) {
    if (item.empty()) {
      return std::nullopt;
    }

    items.push_back(item);
  }

  if (items.empty()) {
    return std::nullopt;
  }

  return items;
}

[[nodiscard]] TimeoutBudget* timeout_budget_for_path(MutableRuntimePolicySnapshot& snapshot,
                                                     std::string_view path) {
  if (starts_with(path, "timeout_policy.llm.")) {
    return &snapshot.timeout_policy.llm;
  }

  if (starts_with(path, "timeout_policy.tool.")) {
    return &snapshot.timeout_policy.tool;
  }

  if (starts_with(path, "timeout_policy.mcp.")) {
    return &snapshot.timeout_policy.mcp;
  }

  if (starts_with(path, "timeout_policy.workflow.")) {
    return &snapshot.timeout_policy.workflow;
  }

  return nullptr;
}

[[nodiscard]] ModelRoutePolicy* stage_route_for_path(MutableRuntimePolicySnapshot& snapshot,
                                                     std::string_view path) {
  constexpr std::string_view kPrefix = "model_profile.";
  constexpr std::string_view kSuffix = ".fallback_route";
  if (!starts_with(path, kPrefix) || !path.ends_with(kSuffix)) {
    return nullptr;
  }

  const std::size_t stage_begin = kPrefix.size();
  const std::size_t stage_length = path.size() - kPrefix.size() - kSuffix.size();
  const std::string stage_name(path.substr(stage_begin, stage_length));
  const auto it = snapshot.model_profile.stage_routes.find(stage_name);
  if (it == snapshot.model_profile.stage_routes.end()) {
    return nullptr;
  }

  return &it->second;
}

[[nodiscard]] bool apply_patch(const ProfileOverridePatch& patch,
                               MutableRuntimePolicySnapshot& snapshot) {
  if (patch.path == "runtime_budget.max_tokens") {
    const auto value = parse_uint32(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.runtime_budget.max_tokens = *value;
    return true;
  }

  if (patch.path == "runtime_budget.max_turns") {
    const auto value = parse_uint32(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.runtime_budget.max_turns = *value;
    return true;
  }

  if (patch.path == "runtime_budget.max_tool_calls") {
    const auto value = parse_uint32(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.runtime_budget.max_tool_calls = *value;
    return true;
  }

  if (patch.path == "runtime_budget.max_latency_ms") {
    const auto value = parse_uint32(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.runtime_budget.max_latency_ms = *value;
    return true;
  }

  if (patch.path == "runtime_budget.max_replan_count") {
    const auto value = parse_uint32(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.runtime_budget.max_replan_count = *value;
    return true;
  }

  if (starts_with(patch.path, "timeout_policy.")) {
    TimeoutBudget* budget = timeout_budget_for_path(snapshot, patch.path);
    if (!budget) {
      return false;
    }

    if (patch.path.ends_with(".timeout_ms")) {
      const auto value = parse_int64(patch.value);
      if (!value.has_value()) {
        return false;
      }
      budget->timeout_ms = *value;
      return true;
    }

    if (patch.path.ends_with(".retry_budget")) {
      const auto value = parse_uint32(patch.value);
      if (!value.has_value()) {
        return false;
      }
      budget->retry_budget = *value;
      return true;
    }

    if (patch.path.ends_with(".circuit_breaker_threshold")) {
      const auto value = parse_uint32(patch.value);
      if (!value.has_value()) {
        return false;
      }
      budget->circuit_breaker_threshold = *value;
      return true;
    }

    return false;
  }

  if (patch.path == "ops_policy.log_level") {
    snapshot.ops_policy.log_level = patch.value;
    return !snapshot.ops_policy.log_level.empty();
  }

  if (patch.path == "ops_policy.metrics_granularity") {
    snapshot.ops_policy.metrics_granularity = patch.value;
    return !snapshot.ops_policy.metrics_granularity.empty();
  }

  if (patch.path == "ops_policy.trace_sample_ratio") {
    const auto value = parse_double(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.ops_policy.trace_sample_ratio = *value;
    return true;
  }

  if (patch.path == "ops_policy.remote_diagnostics_enabled") {
    const auto value = parse_bool(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.ops_policy.remote_diagnostics_enabled = *value;
    return true;
  }

  if (patch.path == "ops_policy.upgrade_strategy") {
    snapshot.ops_policy.upgrade_strategy = patch.value;
    return !snapshot.ops_policy.upgrade_strategy.empty();
  }

  if (patch.path == "capability_cache_policy.refresh_interval_ms") {
    const auto value = parse_int64(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.capability_cache_policy.refresh_interval_ms = *value;
    return true;
  }

  if (patch.path == "capability_cache_policy.expire_after_ms") {
    const auto value = parse_int64(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.capability_cache_policy.expire_after_ms = *value;
    return true;
  }

  if (patch.path == "capability_cache_policy.stale_read_allowed") {
    const auto value = parse_bool(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.capability_cache_policy.stale_read_allowed = *value;
    return true;
  }

  if (patch.path == "capability_cache_policy.failure_backoff_ms") {
    const auto value = parse_int64(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.capability_cache_policy.failure_backoff_ms = *value;
    return true;
  }

  if (patch.path == "degrade_policy.fallback_chain") {
    const auto value = parse_csv_list(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.degrade_policy.fallback_chain = *value;
    return true;
  }

  if (patch.path == "degrade_policy.allow_model_failover") {
    const auto value = parse_bool(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.degrade_policy.allow_model_failover = *value;
    return true;
  }

  if (patch.path == "degrade_policy.allow_budget_degrade") {
    const auto value = parse_bool(patch.value);
    if (!value.has_value()) {
      return false;
    }
    snapshot.degrade_policy.allow_budget_degrade = *value;
    return true;
  }

  if (starts_with(patch.path, "model_profile.")) {
    ModelRoutePolicy* route_policy = stage_route_for_path(snapshot, patch.path);
    if (!route_policy) {
      return false;
    }

    if (patch.op == ProfileOverridePatchOp::Remove) {
      route_policy->fallback_route = std::nullopt;
      return true;
    }

    route_policy->fallback_route = patch.value;
    return route_policy->fallback_route.has_value() && !route_policy->fallback_route->empty();
  }

  return false;
}

[[nodiscard]] ProfileOverlayComposeResult make_override_invalid_result(
    std::vector<std::string> rejected_paths) {
  return ProfileOverlayComposeResult{
      .snapshot = nullptr,
      .rejected_paths = std::move(rejected_paths),
      .error_code = ProfileErrorCode::OverrideInvalid,
  };
}

[[nodiscard]] std::optional<std::vector<std::string>> validate_override(
    const ProfileOverrideInput& input,
    const RuntimePolicySnapshot& base) {
  if (!input.has_consistent_values()) {
    return std::vector<std::string>{"override-metadata"};
  }

  if (input.base_version != base.generation()) {
    return std::vector<std::string>{"base_version"};
  }

  if (input.layer == ProfileOverrideLayer::Deployment && !is_deployment_source(input.source_kind)) {
    return std::vector<std::string>{"source_kind"};
  }

  if (input.layer == ProfileOverrideLayer::Runtime) {
    if (!is_runtime_source(input.source_kind)) {
      return std::vector<std::string>{"source_kind"};
    }

    if (!is_runtime_scope(input.target_scope)) {
      return std::vector<std::string>{"target_scope"};
    }
  }

  std::vector<std::string> rejected_paths;
  for (const ProfileOverridePatch& patch : input.patches) {
    if (!patch.has_consistent_values()) {
      rejected_paths.push_back(patch.path.empty() ? std::string("patch") : patch.path);
      continue;
    }

    if (is_forbidden_path(patch.path)) {
      rejected_paths.push_back(patch.path);
      continue;
    }

    const bool path_allowed = input.layer == ProfileOverrideLayer::Deployment
                                  ? is_deployment_path_allowed(patch.path)
                                  : is_runtime_path_allowed(patch.path);
    if (!path_allowed) {
      rejected_paths.push_back(patch.path);
    }
  }

  if (!rejected_paths.empty()) {
    return rejected_paths;
  }

  return std::nullopt;
}

}  // namespace

bool ProfileOverrideInput::has_consistent_values() const {
  if (override_id.empty() || source_id.empty() || issued_by.empty() || reason_code.empty() ||
      base_version == 0U || patches.empty()) {
    return false;
  }

  if (layer == ProfileOverrideLayer::Runtime && !expires_at_epoch_ms.has_value()) {
    return false;
  }

  return std::all_of(patches.begin(), patches.end(), [](const ProfileOverridePatch& patch) {
    return patch.has_consistent_values();
  });
}

ProfileOverlayComposeResult ProfileOverlayComposer::compose(
    const RuntimePolicySnapshot& base,
    const std::optional<ProfileOverrideInput>& deployment_override,
    const std::optional<ProfileOverrideInput>& runtime_override) const {
  if (!base.has_consistent_values()) {
    return make_override_invalid_result({"base"});
  }

  MutableRuntimePolicySnapshot working_copy = make_mutable_snapshot(base);
  std::uint64_t applied_layers = 0U;

  const auto apply_layer = [&](const std::optional<ProfileOverrideInput>& override_input)
      -> std::optional<ProfileOverlayComposeResult> {
    if (!override_input.has_value()) {
      return std::nullopt;
    }

    const auto rejected_paths = validate_override(*override_input, base);
    if (rejected_paths.has_value()) {
      return make_override_invalid_result(*rejected_paths);
    }

    for (const ProfileOverridePatch& patch : override_input->patches) {
      if (!apply_patch(patch, working_copy)) {
        return make_override_invalid_result({patch.path});
      }
    }

    ++applied_layers;
    return std::nullopt;
  };

  if (const auto invalid = apply_layer(deployment_override); invalid.has_value()) {
    return *invalid;
  }

  if (const auto invalid = apply_layer(runtime_override); invalid.has_value()) {
    return *invalid;
  }

  working_copy.generation = base.generation() + applied_layers;
  RuntimePolicySnapshot composed_snapshot = freeze_snapshot(working_copy);
  if (!composed_snapshot.has_consistent_values()) {
    return make_override_invalid_result({"snapshot-consistency"});
  }

  return ProfileOverlayComposeResult{
      .snapshot = std::make_shared<const RuntimePolicySnapshot>(std::move(composed_snapshot)),
      .rejected_paths = {},
      .error_code = std::nullopt,
  };
}

}  // namespace dasall::profiles
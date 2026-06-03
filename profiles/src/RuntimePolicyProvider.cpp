#include "RuntimePolicyProvider.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "LastKnownGoodStore.h"
#include "ProfileError.h"
#include "ProfileYamlParser.h"

namespace dasall::profiles {
namespace {

template <typename T>
[[nodiscard]] std::optional<T> get_numeric(const std::unordered_map<std::string, std::string>& scalars,
                                           const std::string& key) {
  const auto it = scalars.find(key);
  if (it == scalars.end()) {
    return std::nullopt;
  }

  try {
    if constexpr (std::is_same_v<T, std::uint32_t>) {
      return static_cast<std::uint32_t>(std::stoul(it->second));
    }

    if constexpr (std::is_same_v<T, std::int64_t>) {
      return static_cast<std::int64_t>(std::stoll(it->second));
    }

    if constexpr (std::is_same_v<T, double>) {
      return std::stod(it->second);
    }
  } catch (...) {
    return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<bool> get_bool(const std::unordered_map<std::string, std::string>& scalars,
                                           const std::string& key) {
  const auto it = scalars.find(key);
  if (it == scalars.end()) {
    return std::nullopt;
  }

  if (it->second == "true") {
    return true;
  }

  if (it->second == "false") {
    return false;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> get_string(
    const std::unordered_map<std::string, std::string>& scalars,
    const std::string& key) {
  const auto it = scalars.find(key);
  if (it == scalars.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

[[nodiscard]] std::optional<std::vector<std::string>> get_list(
    const std::unordered_map<std::string, std::vector<std::string>>& lists,
    const std::string& key) {
  const auto it = lists.find(key);
  if (it == lists.end() || it->second.empty()) {
    return std::nullopt;
  }

  return it->second;
}

[[nodiscard]] std::optional<ModelRoutePolicy> get_model_route_policy(
        const ParsedProfileYaml& parsed_yaml,
        const std::string& stage_name) {
    const std::string stage_prefix = "model_profile." + stage_name;
    const auto route = get_string(parsed_yaml.scalar_values, stage_prefix + ".route");
    const auto streaming_enabled =
            get_bool(parsed_yaml.scalar_values, stage_prefix + ".streaming_enabled");
    if (!route.has_value() || !streaming_enabled.has_value()) {
        return std::nullopt;
    }

    return ModelRoutePolicy{
            .route = *route,
            .fallback_route = get_string(parsed_yaml.scalar_values, stage_prefix + ".fallback_route"),
            .streaming_enabled = *streaming_enabled,
    };
}

[[nodiscard]] std::optional<RuntimePolicySnapshot> build_snapshot(
    const std::string& requested_profile_id,
    const ParsedProfileYaml& parsed_yaml) {
  const auto schema_version = get_string(parsed_yaml.scalar_values, "schema_version");
  const auto profile_id = get_string(parsed_yaml.scalar_values, "profile_meta.profile_id");
  if (!schema_version.has_value() || *schema_version != "1" || !profile_id.has_value() ||
      *profile_id != requested_profile_id) {
    return std::nullopt;
  }

        const auto perception_route = get_model_route_policy(parsed_yaml, "perception");
        const auto planning_route = get_model_route_policy(parsed_yaml, "planning");
        const auto execution_route = get_model_route_policy(parsed_yaml, "execution");
        const auto reflection_route = get_model_route_policy(parsed_yaml, "reflection");
        const auto response_route = get_model_route_policy(parsed_yaml, "response");
        if (!perception_route.has_value() || !planning_route.has_value() ||
            !execution_route.has_value() || !reflection_route.has_value() ||
            !response_route.has_value()) {
    return std::nullopt;
  }

  const auto allowed_prompt_releases =
      get_list(parsed_yaml.list_values, "prompt_policy.allowed_prompt_releases");
  const auto trusted_sources = get_list(parsed_yaml.list_values, "prompt_policy.trusted_sources");
  const auto tool_visibility_rules =
      get_list(parsed_yaml.list_values, "prompt_policy.tool_visibility_rules");
  const auto fallback_chain = get_list(parsed_yaml.list_values, "degrade_policy.fallback_chain");
  const auto allowed_tool_domains =
      get_list(parsed_yaml.list_values, "execution_policy.allowed_tool_domains");
  if (!allowed_prompt_releases.has_value() || !trusted_sources.has_value() ||
      !tool_visibility_rules.has_value() || !fallback_chain.has_value() ||
      !allowed_tool_domains.has_value()) {
    return std::nullopt;
  }

    const auto worker_threads =
            get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "runtime_budget.worker_threads");
    const auto max_tokens = get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "runtime_budget.max_tokens");
  const auto max_turns = get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "runtime_budget.max_turns");
  const auto max_tool_calls =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "runtime_budget.max_tool_calls");
  const auto max_latency_ms =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "runtime_budget.max_latency_ms");
  const auto max_replan_count =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "runtime_budget.max_replan_count");

  const auto max_input_tokens =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "token_budget_policy.max_input_tokens");
  const auto max_output_tokens =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "token_budget_policy.max_output_tokens");
  const auto max_history_turns =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "token_budget_policy.max_history_turns");
  const auto compression_threshold =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "token_budget_policy.compression_threshold");

  const auto cache_refresh_interval_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "capability_cache_policy.refresh_interval_ms");
  const auto cache_expire_after_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "capability_cache_policy.expire_after_ms");
  const auto cache_stale_read_allowed =
      get_bool(parsed_yaml.scalar_values, "capability_cache_policy.stale_read_allowed");
  const auto cache_failure_backoff_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "capability_cache_policy.failure_backoff_ms");

  const auto allow_model_failover =
      get_bool(parsed_yaml.scalar_values, "degrade_policy.allow_model_failover");
  const auto allow_budget_degrade =
      get_bool(parsed_yaml.scalar_values, "degrade_policy.allow_budget_degrade");
  const auto multi_agent_enabled =
      get_bool(parsed_yaml.scalar_values, "enabled_modules.multi_agent");
  const auto memory_maintenance_enabled =
      get_bool(parsed_yaml.scalar_values, "memory.maintenance.enabled");
  const auto memory_maintenance_interval_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values,
                                "memory.maintenance.interval_ms");
  const auto memory_maintenance_jitter_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values,
                                "memory.maintenance.jitter_ms");
  const auto memory_maintenance_retention_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values,
                                "memory.maintenance.retention_ms");
  const auto memory_maintenance_checkpoint_strategy =
      get_string(parsed_yaml.scalar_values,
                 "memory.maintenance.checkpoint_strategy");

  const auto execution_requires_confirmation =
      get_bool(parsed_yaml.scalar_values, "execution_policy.requires_high_risk_confirmation");
  const auto execution_safe_mode =
      get_bool(parsed_yaml.scalar_values, "execution_policy.safe_mode_enabled");
  const auto execution_audit_level = get_string(parsed_yaml.scalar_values, "execution_policy.audit_level");

  const auto ops_log_level = get_string(parsed_yaml.scalar_values, "ops_policy.log_level");
  const auto ops_metrics_granularity =
      get_string(parsed_yaml.scalar_values, "ops_policy.metrics_granularity");
  const auto ops_trace_sample_ratio =
      get_numeric<double>(parsed_yaml.scalar_values, "ops_policy.trace_sample_ratio");
  const auto ops_remote_diagnostics_enabled =
      get_bool(parsed_yaml.scalar_values, "ops_policy.remote_diagnostics_enabled");
  const auto ops_upgrade_strategy = get_string(parsed_yaml.scalar_values, "ops_policy.upgrade_strategy");
  const auto metrics_exporter_type =
      get_string(parsed_yaml.scalar_values, "infra.metrics.exporter.type");
  const auto metrics_exporter_package_asset =
      get_string(parsed_yaml.scalar_values, "infra.metrics.exporter.package_asset");
  const auto trace_exporter_type =
      get_string(parsed_yaml.scalar_values, "infra.tracing.exporter.type");
  const auto trace_exporter_otlp_endpoint =
      get_string(parsed_yaml.scalar_values, "infra.tracing.exporter.otlp_endpoint")
          .value_or(std::string());
  const auto trace_exporter_package_asset =
      get_string(parsed_yaml.scalar_values, "infra.tracing.exporter.package_asset");
  const auto secret_backend_type =
      get_string(parsed_yaml.scalar_values, "infra.secret.backend.type");
  const auto secret_backend_package_asset =
      get_string(parsed_yaml.scalar_values, "infra.secret.backend.package_asset");

    if (!worker_threads.has_value() || !max_tokens.has_value() || !max_turns.has_value() ||
      !max_latency_ms.has_value() || !max_replan_count.has_value() ||
      !max_input_tokens.has_value() || !max_output_tokens.has_value() ||
      !max_history_turns.has_value() || !compression_threshold.has_value() ||
      !cache_refresh_interval_ms.has_value() || !cache_expire_after_ms.has_value() ||
      !cache_stale_read_allowed.has_value() || !cache_failure_backoff_ms.has_value() ||
            !allow_model_failover.has_value() || !allow_budget_degrade.has_value() ||
            !multi_agent_enabled.has_value() || !memory_maintenance_enabled.has_value() ||
            !memory_maintenance_interval_ms.has_value() ||
            !memory_maintenance_jitter_ms.has_value() ||
            !memory_maintenance_retention_ms.has_value() ||
            !memory_maintenance_checkpoint_strategy.has_value() ||
      !execution_requires_confirmation.has_value() || !execution_safe_mode.has_value() ||
      !execution_audit_level.has_value() || !ops_log_level.has_value() ||
      !ops_metrics_granularity.has_value() || !ops_trace_sample_ratio.has_value() ||
                        !ops_remote_diagnostics_enabled.has_value() || !ops_upgrade_strategy.has_value() ||
            !metrics_exporter_type.has_value() || !metrics_exporter_package_asset.has_value() ||
            !trace_exporter_type.has_value() || !trace_exporter_package_asset.has_value() ||
            !secret_backend_type.has_value() || !secret_backend_package_asset.has_value()) {
    return std::nullopt;
  }

  const auto planner_timeout_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "timeout_policy.llm.timeout_ms");
  const auto planner_retry_budget =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "timeout_policy.llm.retry_budget");
  const auto planner_circuit_breaker_threshold = get_numeric<std::uint32_t>(
      parsed_yaml.scalar_values, "timeout_policy.llm.circuit_breaker_threshold");

  const auto tool_timeout_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "timeout_policy.tool.timeout_ms");
  const auto tool_retry_budget =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "timeout_policy.tool.retry_budget");
  const auto tool_circuit_breaker_threshold = get_numeric<std::uint32_t>(
      parsed_yaml.scalar_values, "timeout_policy.tool.circuit_breaker_threshold");

  const auto mcp_timeout_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "timeout_policy.mcp.timeout_ms");
  const auto mcp_retry_budget =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "timeout_policy.mcp.retry_budget");
  const auto mcp_circuit_breaker_threshold = get_numeric<std::uint32_t>(
      parsed_yaml.scalar_values, "timeout_policy.mcp.circuit_breaker_threshold");

  const auto workflow_timeout_ms =
      get_numeric<std::int64_t>(parsed_yaml.scalar_values, "timeout_policy.workflow.timeout_ms");
  const auto workflow_retry_budget =
      get_numeric<std::uint32_t>(parsed_yaml.scalar_values, "timeout_policy.workflow.retry_budget");
  const auto workflow_circuit_breaker_threshold = get_numeric<std::uint32_t>(
      parsed_yaml.scalar_values, "timeout_policy.workflow.circuit_breaker_threshold");

  if (!planner_timeout_ms.has_value() || !planner_retry_budget.has_value() ||
      !planner_circuit_breaker_threshold.has_value() || !tool_timeout_ms.has_value() ||
      !tool_retry_budget.has_value() || !tool_circuit_breaker_threshold.has_value() ||
      !mcp_timeout_ms.has_value() || !mcp_retry_budget.has_value() ||
      !mcp_circuit_breaker_threshold.has_value() || !workflow_timeout_ms.has_value() ||
      !workflow_retry_budget.has_value() || !workflow_circuit_breaker_threshold.has_value()) {
    return std::nullopt;
  }

  RuntimePolicySnapshot snapshot{
      1U,
      *profile_id,
      dasall::contracts::RuntimeBudget{
          .max_tokens = *max_tokens,
          .max_turns = *max_turns,
          .max_tool_calls = *max_tool_calls,
          .max_latency_ms = *max_latency_ms,
          .max_replan_count = *max_replan_count,
      },
      ModelProfile{
          .stage_routes = {
              {"perception", *perception_route},
              {"planning", *planning_route},
              {"execution", *execution_route},
              {"reflection", *reflection_route},
              {"response", *response_route},
          },
      },
      TokenBudgetPolicy{
          .max_input_tokens = *max_input_tokens,
          .max_output_tokens = *max_output_tokens,
          .max_history_turns = *max_history_turns,
          .compression_threshold = *compression_threshold,
      },
      PromptPolicy{
          .allowed_prompt_releases = *allowed_prompt_releases,
          .trusted_sources = *trusted_sources,
          .tool_visibility_rules = *tool_visibility_rules,
      },
      CapabilityCachePolicy{
          .refresh_interval_ms = *cache_refresh_interval_ms,
          .expire_after_ms = *cache_expire_after_ms,
          .stale_read_allowed = *cache_stale_read_allowed,
          .failure_backoff_ms = *cache_failure_backoff_ms,
      },
      DegradePolicy{
          .fallback_chain = *fallback_chain,
          .allow_model_failover = *allow_model_failover,
          .allow_budget_degrade = *allow_budget_degrade,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = *planner_timeout_ms,
                               .retry_budget = *planner_retry_budget,
                               .circuit_breaker_threshold = *planner_circuit_breaker_threshold},
          .tool = TimeoutBudget{.timeout_ms = *tool_timeout_ms,
                                .retry_budget = *tool_retry_budget,
                                .circuit_breaker_threshold = *tool_circuit_breaker_threshold},
          .mcp = TimeoutBudget{.timeout_ms = *mcp_timeout_ms,
                               .retry_budget = *mcp_retry_budget,
                               .circuit_breaker_threshold = *mcp_circuit_breaker_threshold},
          .workflow = TimeoutBudget{.timeout_ms = *workflow_timeout_ms,
                                    .retry_budget = *workflow_retry_budget,
                                    .circuit_breaker_threshold = *workflow_circuit_breaker_threshold},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = *execution_requires_confirmation,
          .safe_mode_enabled = *execution_safe_mode,
          .audit_level = *execution_audit_level,
          .allowed_tool_domains = *allowed_tool_domains,
      },
      OpsPolicy{
          .log_level = *ops_log_level,
          .metrics_granularity = *ops_metrics_granularity,
          .trace_sample_ratio = *ops_trace_sample_ratio,
          .remote_diagnostics_enabled = *ops_remote_diagnostics_enabled,
          .upgrade_strategy = *ops_upgrade_strategy,
          .optional_backends = OpsPolicy::OptionalBackendPolicy{
              .metrics_exporter_type = *metrics_exporter_type,
              .metrics_exporter_package_asset = *metrics_exporter_package_asset,
              .trace_exporter_type = *trace_exporter_type,
              .trace_exporter_otlp_endpoint = trace_exporter_otlp_endpoint,
              .trace_exporter_package_asset = *trace_exporter_package_asset,
              .secret_backend_type = *secret_backend_type,
              .secret_backend_package_asset = *secret_backend_package_asset,
          },
      },
      *worker_threads,
      *multi_agent_enabled,
      MemoryMaintenancePolicy{
          .enabled = *memory_maintenance_enabled,
          .interval_ms = *memory_maintenance_interval_ms,
          .jitter_ms = *memory_maintenance_jitter_ms,
          .retention_ms = *memory_maintenance_retention_ms,
          .checkpoint_strategy = *memory_maintenance_checkpoint_strategy,
      },
  };

  if (!snapshot.has_consistent_values()) {
    return std::nullopt;
  }

  return snapshot;
}

}  // namespace

RuntimePolicyProvider::RuntimePolicyProvider(const IProfileCatalog& catalog)
    : RuntimePolicyProvider(catalog, std::make_shared<LastKnownGoodStore>()) {}

RuntimePolicyProvider::RuntimePolicyProvider(const IProfileCatalog& catalog,
                                             std::shared_ptr<ILastKnownGoodStore> lkg_store)
    : catalog_(catalog), lkg_store_(std::move(lkg_store)) {}

RuntimePolicyLoadResult RuntimePolicyProvider::load_snapshot(
    const RuntimePolicyLoadRequest& request) const {
  if (!request.has_consistent_values()) {
    return RuntimePolicyLoadResult{
        .snapshot = nullptr,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

  const ProfileCatalogLookupResult profile_lookup = catalog_.get_profile(request.profile_id);
  if (!profile_lookup.ok()) {
        const RuntimePolicyLoadResult from_lkg = load_from_last_known_good(request.profile_id);
        if (from_lkg.ok()) {
            return from_lkg;
        }

        return RuntimePolicyLoadResult{.snapshot = nullptr, .error_code = from_lkg.error_code};
  }

  const ParsedProfileYaml parsed_yaml =
      parse_profile_yaml_file(profile_lookup.profile->asset_paths.runtime_policy_path);
  if (!parsed_yaml.ok) {
        const RuntimePolicyLoadResult from_lkg = load_from_last_known_good(request.profile_id);
        if (from_lkg.ok()) {
            return from_lkg;
        }

        return RuntimePolicyLoadResult{.snapshot = nullptr, .error_code = from_lkg.error_code};
  }

  const auto snapshot = build_snapshot(request.profile_id, parsed_yaml);
  if (!snapshot.has_value()) {
        const RuntimePolicyLoadResult from_lkg = load_from_last_known_good(request.profile_id);
        if (from_lkg.ok()) {
            return from_lkg;
        }

        return RuntimePolicyLoadResult{.snapshot = nullptr, .error_code = from_lkg.error_code};
  }

  return RuntimePolicyLoadResult{
      .snapshot = std::make_shared<const RuntimePolicySnapshot>(*snapshot),
      .error_code = std::nullopt,
  };
}

RuntimePolicyActivateResult RuntimePolicyProvider::activate_snapshot(
    const RuntimePolicyActivateRequest& request) {
  if (!request.snapshot || !request.snapshot->has_consistent_values()) {
    return RuntimePolicyActivateResult{
        .activated_generation = 0U,
        .error_code = ProfileErrorCode::SchemaInvalid,
    };
  }

    if (!lkg_store_) {
        return RuntimePolicyActivateResult{
                .activated_generation = 0U,
                .error_code = ProfileErrorCode::LastKnownGoodUnavailable,
        };
    }

    const LastKnownGoodSaveResult save_result = lkg_store_->save(request.snapshot);
    if (!save_result.ok()) {
        return RuntimePolicyActivateResult{
                .activated_generation = 0U,
                .error_code = save_result.error_code,
        };
    }

  std::lock_guard<std::mutex> lock(active_snapshot_mutex_);
  active_snapshot_ = request.snapshot;

  return RuntimePolicyActivateResult{
      .activated_generation = request.snapshot->generation(),
      .error_code = std::nullopt,
  };
}

std::shared_ptr<const RuntimePolicySnapshot> RuntimePolicyProvider::active_snapshot() const {
  std::lock_guard<std::mutex> lock(active_snapshot_mutex_);
  return active_snapshot_;
}

RuntimePolicyLoadResult RuntimePolicyProvider::load_from_last_known_good(
        const std::string& profile_id) const {
    if (!lkg_store_) {
        return RuntimePolicyLoadResult{
                .snapshot = nullptr,
                .error_code = ProfileErrorCode::LastKnownGoodUnavailable,
        };
    }

    const LastKnownGoodLoadResult lkg_result = lkg_store_->load(profile_id);
    if (!lkg_result.ok()) {
        return RuntimePolicyLoadResult{
                .snapshot = nullptr,
                .error_code = lkg_result.error_code,
        };
    }

    return RuntimePolicyLoadResult{
            .snapshot = lkg_result.snapshot,
            .error_code = std::nullopt,
    };
}

}  // namespace dasall::profiles

#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "checkpoint/RuntimeBudget.h"

namespace dasall::profiles {

struct ModelRoutePolicy {
  std::string route;
  std::optional<std::string> fallback_route;
  bool streaming_enabled = false;

  [[nodiscard]] bool has_consistent_values() const {
    return !route.empty() && (!fallback_route.has_value() || !fallback_route->empty());
  }
};

struct ModelProfile {
  std::map<std::string, ModelRoutePolicy> stage_routes;

  [[nodiscard]] bool has_consistent_values() const {
    if (stage_routes.empty()) {
      return false;
    }

    return std::all_of(stage_routes.begin(), stage_routes.end(), [](const auto& entry) {
      return !entry.first.empty() && entry.second.has_consistent_values();
    });
  }
};

struct TokenBudgetPolicy {
  std::uint32_t max_input_tokens = 0;
  std::uint32_t max_output_tokens = 0;
  std::uint32_t max_history_turns = 0;
  std::uint32_t compression_threshold = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return max_input_tokens > 0U && max_output_tokens > 0U && max_history_turns > 0U &&
           compression_threshold > 0U;
  }
};

struct PromptPolicy {
  std::vector<std::string> allowed_prompt_releases;
  std::vector<std::string> trusted_sources;
  std::vector<std::string> tool_visibility_rules;

  [[nodiscard]] bool has_consistent_values() const {
    return !allowed_prompt_releases.empty() && !trusted_sources.empty() &&
           has_unique_values(allowed_prompt_releases) && has_unique_values(trusted_sources) &&
           has_unique_values(tool_visibility_rules);
  }

 private:
  [[nodiscard]] static bool has_unique_values(const std::vector<std::string>& values) {
    std::vector<std::string> sorted_values = values;
    std::sort(sorted_values.begin(), sorted_values.end());
    return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
  }
};

struct CapabilityCachePolicy {
  std::int64_t refresh_interval_ms = 0;
  std::int64_t expire_after_ms = 0;
  bool stale_read_allowed = false;
  std::int64_t failure_backoff_ms = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return refresh_interval_ms > 0 && expire_after_ms >= refresh_interval_ms &&
           failure_backoff_ms >= 0;
  }
};

struct DegradePolicy {
  std::vector<std::string> fallback_chain;
  bool allow_model_failover = false;
  bool allow_budget_degrade = false;

  [[nodiscard]] bool has_consistent_values() const {
    return !fallback_chain.empty();
  }
};

struct TimeoutBudget {
  std::int64_t timeout_ms = 0;
  std::uint32_t retry_budget = 0;
  std::uint32_t circuit_breaker_threshold = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return timeout_ms > 0 && circuit_breaker_threshold > 0U;
  }
};

struct TimeoutPolicy {
  TimeoutBudget llm;
  TimeoutBudget tool;
  TimeoutBudget mcp;
  TimeoutBudget workflow;

  [[nodiscard]] bool has_consistent_values() const {
    return llm.has_consistent_values() && tool.has_consistent_values() &&
           mcp.has_consistent_values() && workflow.has_consistent_values();
  }
};

struct ExecutionPolicy {
  bool requires_high_risk_confirmation = true;
  bool safe_mode_enabled = true;
  std::string audit_level;
  std::vector<std::string> allowed_tool_domains;

  [[nodiscard]] bool has_consistent_values() const {
    return !audit_level.empty();
  }
};

struct OpsPolicy {
  std::string log_level;
  std::string metrics_granularity;
  double trace_sample_ratio = 0.0;
  bool remote_diagnostics_enabled = false;
  std::string upgrade_strategy;

  [[nodiscard]] bool has_consistent_values() const {
    return !log_level.empty() && !metrics_granularity.empty() && !upgrade_strategy.empty() &&
           trace_sample_ratio >= 0.0 && trace_sample_ratio <= 1.0;
  }
};

class RuntimePolicySnapshot {
 public:
  RuntimePolicySnapshot(std::uint64_t generation,
                        std::string effective_profile_id,
                        contracts::RuntimeBudget runtime_budget,
                        ModelProfile model_profile,
                        TokenBudgetPolicy token_budget_policy,
                        PromptPolicy prompt_policy,
                        CapabilityCachePolicy capability_cache_policy,
                        DegradePolicy degrade_policy,
                        TimeoutPolicy timeout_policy,
                        ExecutionPolicy execution_policy,
                        OpsPolicy ops_policy,
                        std::uint32_t worker_threads = 1U)
      : generation_(generation),
        effective_profile_id_(std::move(effective_profile_id)),
        runtime_budget_(std::move(runtime_budget)),
        model_profile_(std::move(model_profile)),
        token_budget_policy_(std::move(token_budget_policy)),
        prompt_policy_(std::move(prompt_policy)),
        capability_cache_policy_(std::move(capability_cache_policy)),
        degrade_policy_(std::move(degrade_policy)),
        timeout_policy_(std::move(timeout_policy)),
        execution_policy_(std::move(execution_policy)),
        ops_policy_(std::move(ops_policy)),
        worker_threads_(worker_threads) {}

  [[nodiscard]] std::uint64_t generation() const {
    return generation_;
  }

  [[nodiscard]] const std::string& effective_profile_id() const {
    return effective_profile_id_;
  }

  [[nodiscard]] const contracts::RuntimeBudget& runtime_budget() const {
    return runtime_budget_;
  }

  [[nodiscard]] const ModelProfile& model_profile() const {
    return model_profile_;
  }

  [[nodiscard]] const TokenBudgetPolicy& token_budget_policy() const {
    return token_budget_policy_;
  }

  [[nodiscard]] const PromptPolicy& prompt_policy() const {
    return prompt_policy_;
  }

  [[nodiscard]] const CapabilityCachePolicy& capability_cache_policy() const {
    return capability_cache_policy_;
  }

  [[nodiscard]] const DegradePolicy& degrade_policy() const {
    return degrade_policy_;
  }

  [[nodiscard]] const TimeoutPolicy& timeout_policy() const {
    return timeout_policy_;
  }

  [[nodiscard]] const ExecutionPolicy& execution_policy() const {
    return execution_policy_;
  }

  [[nodiscard]] const OpsPolicy& ops_policy() const {
    return ops_policy_;
  }

  [[nodiscard]] std::uint32_t worker_threads() const {
    return worker_threads_;
  }

  [[nodiscard]] bool has_consistent_values() const {
    return generation_ > 0U && !effective_profile_id_.empty() &&
           runtime_budget_.max_tokens.has_value() && runtime_budget_.max_turns.has_value() &&
           runtime_budget_.max_tool_calls.has_value() &&
           runtime_budget_.max_latency_ms.has_value() &&
           runtime_budget_.max_replan_count.has_value() && model_profile_.has_consistent_values() &&
           token_budget_policy_.has_consistent_values() && prompt_policy_.has_consistent_values() &&
           capability_cache_policy_.has_consistent_values() &&
           degrade_policy_.has_consistent_values() && timeout_policy_.has_consistent_values() &&
           execution_policy_.has_consistent_values() && ops_policy_.has_consistent_values() &&
           worker_threads_ > 0U;
  }

 private:
  std::uint64_t generation_;
  std::string effective_profile_id_;
  contracts::RuntimeBudget runtime_budget_;
  ModelProfile model_profile_;
  TokenBudgetPolicy token_budget_policy_;
  PromptPolicy prompt_policy_;
  CapabilityCachePolicy capability_cache_policy_;
  DegradePolicy degrade_policy_;
  TimeoutPolicy timeout_policy_;
  ExecutionPolicy execution_policy_;
  OpsPolicy ops_policy_;
  std::uint32_t worker_threads_;
};

}  // namespace dasall::profiles
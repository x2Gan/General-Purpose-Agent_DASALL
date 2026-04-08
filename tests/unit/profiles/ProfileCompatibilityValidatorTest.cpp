#include <exception>
#include <iostream>

#include "BuildProfileManifest.h"
#include "ProfileCompatibilityValidator.h"
#include "ProfileError.h"
#include "RuntimePolicySnapshot.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_snapshot() {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return RuntimePolicySnapshot{
      9U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 10U,
          .max_latency_ms = 5000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"planner", ModelRoutePolicy{.route = "cloud.reasoning", .fallback_route = "lan.general"}},
              {"responder", ModelRoutePolicy{.route = "cloud.general", .fallback_route = "local.small"}},
          },
      },
      TokenBudgetPolicy{.max_input_tokens = 4000U,
                        .max_output_tokens = 1000U,
                        .max_history_turns = 8U,
                        .compression_threshold = 3000U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 1000,
                            .expire_after_ms = 5000,
                            .stale_read_allowed = false,
                            .failure_backoff_ms = 500},
      DegradePolicy{.fallback_chain = {"lan.general"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = true},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
                    .tool = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
                    .mcp = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 3U},
                    .workflow = TimeoutBudget{.timeout_ms = 2000, .retry_budget = 1U, .circuit_breaker_threshold = 3U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin", "mcp"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "full",
                .trace_sample_ratio = 0.2,
                .remote_diagnostics_enabled = true,
                .upgrade_strategy = "rolling"},
  };
}

[[nodiscard]] dasall::profiles::BuildProfileManifest make_manifest() {
  return dasall::profiles::BuildProfileManifest{
      .enabled_modules = {"runtime", "cognition", "infra_observability", "llm_cloud_adapter"},
      .enabled_adapters = {"llm_cloud_adapter"},
      .observability_level = "full",
      .build_tags = {"profile:desktop_full", "platform:linux-x86_64-workstation", "support:ga"},
      .toolchain_hint = std::string("x86_64-linux-gnu"),
  };
}

void test_validator_allows_compatible_environment() {
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileCompatibilityValidator;
  using dasall::profiles::ProfileRuntimeEnvironment;
  using dasall::tests::support::assert_true;

  const ProfileCompatibilityValidator validator;
  const auto report = validator.validate(
      make_snapshot(),
      make_manifest(),
      ProfileRuntimeEnvironment{.target_platform = "linux-x86_64-workstation",
                                .available_modules = {"runtime", "cognition", "infra_observability", "llm_cloud_adapter"},
                                .available_adapters = {"llm_cloud_adapter"}});

  assert_true(report.can_activate(), "validator should allow compatible environment");
  assert_true(report.compatibility_state == ProfileCompatibilityState::Compatible,
              "validator should mark fully matching environment as compatible");
  assert_true(report.has_consistent_values(), "validator report should satisfy consistency constraints");
}

void test_validator_blocks_missing_required_adapter() {
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileCompatibilityValidator;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ProfileRuntimeEnvironment;
  using dasall::tests::support::assert_true;

  const ProfileCompatibilityValidator validator;
  const auto report = validator.validate(
      make_snapshot(),
      make_manifest(),
      ProfileRuntimeEnvironment{.target_platform = "linux-x86_64-workstation",
                                .available_modules = {"runtime", "cognition", "infra_observability", "llm_cloud_adapter"},
                                .available_adapters = {}});

  assert_true(!report.can_activate(), "validator should reject environment without required adapter");
  assert_true(report.compatibility_state == ProfileCompatibilityState::Blocked,
              "missing required adapter should map to blocked state");
  assert_true(std::find(report.blocking_errors.begin(), report.blocking_errors.end(),
                        ProfileErrorCode::RequiredAdapterMissing) != report.blocking_errors.end(),
              "missing required adapter should map to required-adapter-missing error code");
}

void test_validator_emits_warning_for_optional_observability_module_gap() {
  using dasall::profiles::BuildProfileManifest;
  using dasall::profiles::ProfileCompatibilityState;
  using dasall::profiles::ProfileCompatibilityValidator;
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ProfileRuntimeEnvironment;
  using dasall::tests::support::assert_true;

  const ProfileCompatibilityValidator validator;
  BuildProfileManifest manifest = make_manifest();
  manifest.enabled_modules = {"infra_observability"};
  manifest.enabled_adapters.clear();

  const auto report = validator.validate(
      make_snapshot(),
      manifest,
      ProfileRuntimeEnvironment{.target_platform = "linux-x86_64-workstation",
                                .available_modules = {},
                                .available_adapters = {}});

  assert_true(report.can_activate(),
              "optional observability gap should remain activatable with warning state");
  assert_true(report.compatibility_state == ProfileCompatibilityState::Warning,
              "optional observability gap should map to warning state");
  assert_true(std::find(report.warnings.begin(), report.warnings.end(),
                        ProfileErrorCode::OverrideInvalid) != report.warnings.end(),
              "optional observability gap should be surfaced as warning code");
  assert_true(report.has_consistent_values(), "warning report should keep consistency constraints");
}

}  // namespace

int main() {
  try {
    test_validator_allows_compatible_environment();
    test_validator_blocks_missing_required_adapter();
    test_validator_emits_warning_for_optional_observability_module_gap();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

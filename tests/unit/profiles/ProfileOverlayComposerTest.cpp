#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>

#include "ProfileOverlayComposer.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_base_snapshot() {
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
      7U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
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
                   .trusted_sources = {"profiles", "infra_config"},
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

[[nodiscard]] dasall::profiles::ProfileOverrideInput make_deployment_override() {
  using dasall::profiles::ProfileOverrideInput;
  using dasall::profiles::ProfileOverrideLayer;
  using dasall::profiles::ProfileOverridePatch;
  using dasall::profiles::ProfileOverrideSourceKind;
  using dasall::profiles::ProfileOverrideTargetScope;

  return ProfileOverrideInput{
      .layer = ProfileOverrideLayer::Deployment,
      .override_id = "deploy-override-01",
      .source_kind = ProfileOverrideSourceKind::SiteBundle,
      .source_id = "site-bundle-sha256:01",
      .issued_by = "release-pipeline",
      .target_scope = ProfileOverrideTargetScope::Site,
      .base_version = 7U,
      .expires_at_epoch_ms = std::nullopt,
      .reason_code = "site-calibration",
      .patches = {
          ProfileOverridePatch{.path = "runtime_budget.max_tokens", .value = "6144"},
          ProfileOverridePatch{.path = "ops_policy.log_level", .value = "warn"},
          ProfileOverridePatch{.path = "model_profile.planner.fallback_route", .value = "lan.safe"},
      },
  };
}

[[nodiscard]] dasall::profiles::ProfileOverrideInput make_runtime_override() {
  using dasall::profiles::ProfileOverrideInput;
  using dasall::profiles::ProfileOverrideLayer;
  using dasall::profiles::ProfileOverridePatch;
  using dasall::profiles::ProfileOverrideSourceKind;
  using dasall::profiles::ProfileOverrideTargetScope;

  return ProfileOverrideInput{
      .layer = ProfileOverrideLayer::Runtime,
      .override_id = "runtime-override-01",
      .source_kind = ProfileOverrideSourceKind::RuntimeCommand,
      .source_id = "incident-bridge-42",
      .issued_by = "ops-oncall",
      .target_scope = ProfileOverrideTargetScope::Process,
      .base_version = 7U,
      .expires_at_epoch_ms = 1735689600000ULL,
      .reason_code = "incident-mitigation",
      .patches = {
          ProfileOverridePatch{.path = "runtime_budget.max_tokens", .value = "4096"},
          ProfileOverridePatch{.path = "ops_policy.log_level", .value = "debug"},
          ProfileOverridePatch{.path = "capability_cache_policy.refresh_interval_ms", .value = "250"},
      },
  };
}

void test_overlay_composer_applies_deployment_then_runtime_precedence() {
  using dasall::profiles::ProfileOverlayComposer;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto base = make_base_snapshot();
  const ProfileOverlayComposer composer;

  const auto result = composer.compose(base, make_deployment_override(), make_runtime_override());

  assert_true(result.ok(), "overlay composer should accept valid deployment/runtime overrides");
  assert_true(result.has_consistent_values(), "successful compose result should stay consistent");
  assert_equal(9, static_cast<int>(result.snapshot->generation()),
               "successful compose should bump generation once per applied override layer");
  assert_equal(4096, static_cast<int>(*result.snapshot->runtime_budget().max_tokens),
               "runtime override should win over deployment override on runtime budget");
  assert_equal(std::string("debug"), result.snapshot->ops_policy().log_level,
               "runtime override should win over deployment override on ops log level");
  assert_equal(std::string("lan.safe"), *result.snapshot->model_profile().stage_routes.at("planner").fallback_route,
               "deployment override should remain effective when runtime override does not replace the same key");
  assert_equal(250, static_cast<int>(result.snapshot->capability_cache_policy().refresh_interval_ms),
               "runtime override should update runtime-tunable cache policy field");
}

void test_overlay_composer_rejects_runtime_override_without_ttl() {
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ProfileOverlayComposer;
  using dasall::tests::support::assert_true;

  auto invalid_runtime_override = make_runtime_override();
  invalid_runtime_override.expires_at_epoch_ms = std::nullopt;

  const ProfileOverlayComposer composer;
  const auto result = composer.compose(make_base_snapshot(), std::nullopt, invalid_runtime_override);

  assert_true(!result.ok(), "runtime override without ttl should be rejected");
  assert_true(result.error_code.has_value() && *result.error_code == ProfileErrorCode::OverrideInvalid,
              "ttl violation should surface override-invalid error code");
  assert_true(std::find(result.rejected_paths.begin(), result.rejected_paths.end(), "override-metadata") !=
                  result.rejected_paths.end(),
              "ttl violation should report metadata rejection");
}

void test_overlay_composer_rejects_non_whitelisted_runtime_patch_without_partial_snapshot() {
  using dasall::profiles::ProfileErrorCode;
  using dasall::profiles::ProfileOverlayComposer;
  using dasall::profiles::ProfileOverridePatch;
  using dasall::tests::support::assert_true;

  auto invalid_runtime_override = make_runtime_override();
  invalid_runtime_override.patches.push_back(
      ProfileOverridePatch{.path = "execution_policy.safe_mode_enabled", .value = "false"});

  const ProfileOverlayComposer composer;
  const auto result = composer.compose(make_base_snapshot(), make_deployment_override(), invalid_runtime_override);

  assert_true(!result.ok(), "non-whitelisted runtime patch should be rejected");
  assert_true(result.snapshot == nullptr,
              "invalid runtime patch should not produce partially merged candidate snapshot");
  assert_true(result.error_code.has_value() && *result.error_code == ProfileErrorCode::OverrideInvalid,
              "rejected runtime patch should surface override-invalid error code");
  assert_true(std::find(result.rejected_paths.begin(), result.rejected_paths.end(),
                        "execution_policy.safe_mode_enabled") != result.rejected_paths.end(),
              "rejected path should be reported for auditability");
}

}  // namespace

int main() {
  try {
    test_overlay_composer_applies_deployment_then_runtime_precedence();
    test_overlay_composer_rejects_runtime_override_without_ttl();
    test_overlay_composer_rejects_non_whitelisted_runtime_patch_without_partial_snapshot();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
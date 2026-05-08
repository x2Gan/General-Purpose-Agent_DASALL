#include <exception>
#include <iostream>
#include <string>

#include "config/ConfigCapabilityResolver.h"
#include "support/TestAssertions.h"

namespace {

void test_resolve_capabilities_for_p0_hidden_surface() {
  using dasall::apps::cli::config::ConfigCapabilityInputs;
  using dasall::apps::cli::config::ConfigCapabilityResolver;
  using dasall::apps::cli::config::ToolSkillPageMode;
  using dasall::tests::support::assert_true;

  ConfigCapabilityResolver resolver;
  const auto capabilities = resolver.resolve(ConfigCapabilityInputs{});
  assert_true(capabilities.config_validate_available,
              "ConfigCapabilityResolver should expose validate capability when daemon validate-only is available");
  assert_true(capabilities.service_manager_actions_available,
              "ConfigCapabilityResolver should expose service actions when systemd is available");
  assert_true(!capabilities.llm_secret_onboarding_available,
              "ConfigCapabilityResolver should keep LLM secret onboarding disabled until bootstrap writer and backend are both ready");
  assert_true(capabilities.tool_skill_page_mode == ToolSkillPageMode::Hidden,
              "ConfigCapabilityResolver should keep ToolSkillPage hidden when no active tooling is detected");
  assert_true(capabilities.has_reason("tool_skill_surface_hidden"),
              "Hidden ToolSkillPage mode should surface a stable hidden reason");
}

void test_resolve_capabilities_for_summary_only_tooling() {
  using dasall::apps::cli::config::ConfigCapabilityInputs;
  using dasall::apps::cli::config::ConfigCapabilityResolver;
  using dasall::apps::cli::config::ToolSkillPageMode;
  using dasall::tests::support::assert_true;

  ConfigCapabilityResolver resolver;
  ConfigCapabilityInputs inputs;
  inputs.active_tooling_detected = true;
  const auto capabilities = resolver.resolve(inputs);
  assert_true(capabilities.tool_skill_page_mode == ToolSkillPageMode::SummaryOnly,
              "ConfigCapabilityResolver should expose summary-only tooling when active bundles exist but operator surface is not frozen");
}

void test_resolve_capabilities_for_editable_tooling_and_llm_onboarding() {
  using dasall::apps::cli::config::ConfigCapabilityInputs;
  using dasall::apps::cli::config::ConfigCapabilityResolver;
  using dasall::apps::cli::config::ToolSkillPageMode;
  using dasall::tests::support::assert_true;

  ConfigCapabilityResolver resolver;
  ConfigCapabilityInputs inputs;
  inputs.secret_bootstrap_writer_available = true;
  inputs.secret_backend_available = true;
  inputs.active_tooling_detected = true;
  inputs.tool_skill_operator_surface_ready = true;

  const auto capabilities = resolver.resolve(inputs);
  assert_true(capabilities.llm_secret_onboarding_available,
              "ConfigCapabilityResolver should expose LLM secret onboarding once bootstrap writer and backend are available");
  assert_true(capabilities.tool_skill_page_mode == ToolSkillPageMode::Editable,
              "ConfigCapabilityResolver should only expose editable tooling when operator surface is ready");
}

void test_resolve_capabilities_reports_missing_dependencies() {
  using dasall::apps::cli::config::ConfigCapabilityInputs;
  using dasall::apps::cli::config::ConfigCapabilityResolver;
  using dasall::tests::support::assert_true;

  ConfigCapabilityResolver resolver;
  ConfigCapabilityInputs inputs;
  inputs.daemon_validate_only_available = false;
  inputs.systemd_available = false;
  const auto capabilities = resolver.resolve(inputs);
  assert_true(!capabilities.config_validate_available,
              "ConfigCapabilityResolver should mark validate capability unavailable when daemon validate-only is missing");
  assert_true(!capabilities.service_manager_actions_available,
              "ConfigCapabilityResolver should mark service actions unavailable without systemd");
  assert_true(capabilities.has_reason("daemon_validate_only_unavailable"),
              "Missing validate-only support should be surfaced as a stable unavailable reason");
  assert_true(capabilities.has_reason("systemd_unavailable"),
              "Missing systemd support should be surfaced as a stable unavailable reason");
}

}  // namespace

int main() {
  try {
    test_resolve_capabilities_for_p0_hidden_surface();
    test_resolve_capabilities_for_summary_only_tooling();
    test_resolve_capabilities_for_editable_tooling_and_llm_onboarding();
    test_resolve_capabilities_reports_missing_dependencies();
  } catch (const std::exception& ex) {
    std::cerr << "ConfigCapabilityResolverTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ConfigCapabilityResolverTest passed\n";
  return 0;
}
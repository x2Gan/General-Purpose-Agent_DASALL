#include <exception>
#include <iostream>

#include "config/ConfigCapabilityResolver.h"
#include "config/ToolSkillPage.h"
#include "support/TestAssertions.h"

namespace {

dasall::apps::cli::config::ConfigCapabilitySet make_capabilities(
    const bool active_tooling_detected,
    const bool operator_surface_ready) {
  using dasall::apps::cli::config::ConfigCapabilityInputs;
  using dasall::apps::cli::config::ConfigCapabilityResolver;

  ConfigCapabilityResolver resolver;
  ConfigCapabilityInputs inputs;
  inputs.active_tooling_detected = active_tooling_detected;
  inputs.tool_skill_operator_surface_ready = operator_surface_ready;
  return resolver.resolve(inputs);
}

void test_render_projects_hidden_page_when_no_active_tooling_is_detected() {
  using dasall::apps::cli::config::ToolSkillPage;
  using dasall::apps::cli::config::ToolSkillPageMode;
  using dasall::tests::support::assert_true;

  ToolSkillPage page;
  const auto view = page.render(make_capabilities(false, false));

  assert_true(view.mode == ToolSkillPageMode::Hidden,
              "ToolSkillPage should keep the page hidden when active tooling is absent");
  assert_true(!view.controls_enabled,
              "ToolSkillPage should not enable controls for hidden mode");
  assert_true(view.banner.find("hidden") != std::string::npos,
              "ToolSkillPage should describe hidden mode in the banner");
  assert_true(!view.constraints.empty() &&
                  view.constraints.front().find("active bundle or skill capability") !=
                      std::string::npos,
              "ToolSkillPage should explain why hidden mode is being enforced");
}

void test_render_projects_read_only_summary_when_tooling_is_detected() {
  using dasall::apps::cli::config::ToolSkillPage;
  using dasall::apps::cli::config::ToolSkillPageMode;
  using dasall::tests::support::assert_true;

  ToolSkillPage page;
  const auto view = page.render(make_capabilities(true, false));

  assert_true(view.mode == ToolSkillPageMode::SummaryOnly,
              "ToolSkillPage should project summary-only mode when tooling is detected before the operator surface is frozen");
  assert_true(!view.controls_enabled,
              "ToolSkillPage should keep controls read-only in summary-only mode");
  assert_true(view.banner.find("read-only summary") != std::string::npos,
              "ToolSkillPage should describe summary-only mode in the banner");
  assert_true(!view.summary_items.empty() &&
                  view.summary_items[1] == "controls: read-only",
              "ToolSkillPage should expose stable read-only summary items");
}

void test_render_projects_editable_mode_only_after_operator_surface_is_ready() {
  using dasall::apps::cli::config::ToolSkillPage;
  using dasall::apps::cli::config::ToolSkillPageMode;
  using dasall::tests::support::assert_true;

  ToolSkillPage page;
  const auto view = page.render(make_capabilities(true, true));

  assert_true(view.mode == ToolSkillPageMode::Editable,
              "ToolSkillPage should only enter editable mode once the operator surface is ready");
  assert_true(view.controls_enabled,
              "ToolSkillPage should enable controls in editable mode");
  assert_true(view.banner.find("editable") != std::string::npos,
              "ToolSkillPage should describe editable mode in the banner");
  assert_true(!view.constraints.empty() &&
                  view.constraints.front().find("source-scoped") != std::string::npos,
              "ToolSkillPage should preserve the P2 constraint that edits remain source-scoped and auditable");
}

}  // namespace

int main() {
  try {
    test_render_projects_hidden_page_when_no_active_tooling_is_detected();
    test_render_projects_read_only_summary_when_tooling_is_detected();
    test_render_projects_editable_mode_only_after_operator_surface_is_ready();
  } catch (const std::exception& ex) {
    std::cerr << "ToolSkillPageTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "ToolSkillPageTest passed\n";
  return 0;
}
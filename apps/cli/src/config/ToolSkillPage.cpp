#include "config/ToolSkillPage.h"

#include <string_view>

namespace dasall::apps::cli::config {

namespace {

constexpr std::string_view kHiddenBanner =
    "tools/skills operator surface is hidden for this deployment";
constexpr std::string_view kSummaryOnlyBanner =
    "tools/skills operator surface is available as a read-only summary";
constexpr std::string_view kEditableBanner =
    "tools/skills operator surface is editable for this deployment";
constexpr std::string_view kHiddenConstraint =
    "active bundle or skill capability has not been detected";
constexpr std::string_view kSummaryOnlyConstraint =
    "bundle/source/importer controls remain owner-gated until the operator surface is frozen";
constexpr std::string_view kEditableConstraint =
    "changes must remain source-scoped, auditable, and routed through tools owner adapters";

void add_unique_string(std::vector<std::string>& values,
                       std::string_view value) {
  for (const auto& existing : values) {
    if (existing == value) {
      return;
    }
  }

  values.emplace_back(value);
}

}  // namespace

bool ToolSkillPageView::is_well_formed() const {
  return !banner.empty();
}

ToolSkillPageView ToolSkillPage::render(
    const ConfigCapabilitySet& capabilities) const {
  ToolSkillPageView view;
  view.mode = capabilities.tool_skill_page_mode;
  view.controls_enabled =
      capabilities.tool_skill_page_mode == ToolSkillPageMode::Editable;

  switch (capabilities.tool_skill_page_mode) {
    case ToolSkillPageMode::Hidden:
      view.banner = std::string(kHiddenBanner);
      view.summary_items = {
          "mode: hidden",
          "controls: unavailable",
      };
      view.constraints = {
          std::string(kHiddenConstraint),
      };
      break;
    case ToolSkillPageMode::SummaryOnly:
      view.banner = std::string(kSummaryOnlyBanner);
      view.summary_items = {
          "mode: summary_only",
          "controls: read-only",
          "active tooling: detected",
      };
      view.constraints = {
          std::string(kSummaryOnlyConstraint),
      };
      break;
    case ToolSkillPageMode::Editable:
      view.banner = std::string(kEditableBanner);
      view.summary_items = {
          "mode: editable",
          "controls: editable",
          "active tooling: detected",
      };
      view.constraints = {
          std::string(kEditableConstraint),
      };
      break;
  }

  for (const auto& reason : capabilities.unavailable_reasons) {
    add_unique_string(view.constraints, reason);
  }

  return view;
}

}  // namespace dasall::apps::cli::config
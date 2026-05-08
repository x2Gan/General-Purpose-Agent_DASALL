#pragma once

#include <string>
#include <vector>

#include "config/ConfigCapabilityResolver.h"

namespace dasall::apps::cli::config {

struct ToolSkillPageView {
  ToolSkillPageMode mode = ToolSkillPageMode::Hidden;
  bool controls_enabled = false;
  std::string banner;
  std::vector<std::string> summary_items;
  std::vector<std::string> constraints;

  [[nodiscard]] bool is_well_formed() const;
};

class ToolSkillPage {
 public:
  [[nodiscard]] ToolSkillPageView render(
      const ConfigCapabilitySet& capabilities) const;
};

}  // namespace dasall::apps::cli::config
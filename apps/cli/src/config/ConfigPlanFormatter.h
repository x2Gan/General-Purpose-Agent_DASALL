#pragma once

#include <string>

#include "config/ConfigCommandTypes.h"

namespace dasall::apps::cli::config {

class ConfigPlanFormatter {
 public:
  [[nodiscard]] static std::string format_human(const ConfigActionPlan& plan);

  [[nodiscard]] static std::string format_json(const ConfigActionPlan& plan);
};

}  // namespace dasall::apps::cli::config
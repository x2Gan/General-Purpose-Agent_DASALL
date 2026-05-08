#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "config/ConfigCommandTypes.h"

namespace dasall::apps::cli::config {

struct PrivilegeContext {
  bool running_as_root = false;
  bool stdin_is_tty = false;
};

struct PrivilegeRequirementResult {
  bool root_required = false;
  bool allowed = true;
  std::vector<std::string> failure_reasons;

  [[nodiscard]] bool has_reason(std::string_view reason) const;
};

class PrivilegeProbe {
 public:
  [[nodiscard]] PrivilegeContext current() const;

  [[nodiscard]] PrivilegeRequirementResult require_root_for_write(
      const ConfigActionPlan& plan,
      const PrivilegeContext& context) const;
};

}  // namespace dasall::apps::cli::config
#pragma once

#include <string>

#include "logging/LogTypes.h"

namespace dasall::infra::logging {

struct RedactionFilterOptions {
  bool enabled = true;
  std::string ruleset = "default_v1";
};

class RedactionFilter {
 public:
  explicit RedactionFilter(RedactionFilterOptions options = {});

  void set_options(RedactionFilterOptions options);

  [[nodiscard]] bool enabled() const {
    return options_.enabled;
  }

  [[nodiscard]] const std::string& ruleset() const {
    return options_.ruleset;
  }

  [[nodiscard]] LogEvent apply(const LogEvent& event) const;

 private:
  RedactionFilterOptions options_{};
};

}  // namespace dasall::infra::logging
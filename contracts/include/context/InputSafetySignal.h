#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace dasall::contracts {

struct InputSafetySignal {
  bool injection_detected = false;
  bool pii_detected = false;
  std::vector<std::string> reason_codes;

  [[nodiscard]] bool has_consistent_values() const {
    return std::all_of(reason_codes.begin(), reason_codes.end(),
                       [](const std::string& reason_code) {
                         return !reason_code.empty();
                       });
  }
};

}  // namespace dasall::contracts
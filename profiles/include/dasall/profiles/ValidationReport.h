#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "dasall/profiles/ProfileError.h"

namespace dasall::profiles {

enum class ProfileCompatibilityState {
  Compatible,
  Warning,
  Blocked,
};

struct ValidationReport {
  std::vector<ProfileErrorCode> blocking_errors;
  std::vector<ProfileErrorCode> warnings;
  std::vector<std::string> dependency_gaps;
  ProfileCompatibilityState compatibility_state = ProfileCompatibilityState::Compatible;

  [[nodiscard]] bool can_activate() const {
    return blocking_errors.empty();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!has_unique_codes(blocking_errors) || !has_unique_codes(warnings) ||
        has_overlap(blocking_errors, warnings)) {
      return false;
    }

    if (!blocking_errors.empty()) {
      return compatibility_state == ProfileCompatibilityState::Blocked;
    }

    if (!warnings.empty()) {
      return compatibility_state == ProfileCompatibilityState::Warning;
    }

    return compatibility_state == ProfileCompatibilityState::Compatible;
  }

 private:
  [[nodiscard]] static bool has_unique_codes(const std::vector<ProfileErrorCode>& codes) {
    std::vector<std::uint16_t> sorted_codes;
    sorted_codes.reserve(codes.size());
    for (const ProfileErrorCode code : codes) {
      sorted_codes.push_back(static_cast<std::uint16_t>(code));
    }

    std::sort(sorted_codes.begin(), sorted_codes.end());
    return std::adjacent_find(sorted_codes.begin(), sorted_codes.end()) == sorted_codes.end();
  }

  [[nodiscard]] static bool has_overlap(const std::vector<ProfileErrorCode>& left,
                                        const std::vector<ProfileErrorCode>& right) {
    return std::any_of(left.begin(), left.end(), [&right](const ProfileErrorCode code) {
      return std::find(right.begin(), right.end(), code) != right.end();
    });
  }
};

}  // namespace dasall::profiles
#pragma once

#include <cstddef>
#include <string_view>

#include "boundary/CompatibilityGuards.h"

namespace dasall::contracts {

struct EnumLifecycleDescriptor {
  const int* known_values = nullptr;
  std::size_t known_value_count = 0;
  int unspecified_value = 0;
  const int* deprecated_values = nullptr;
  std::size_t deprecated_value_count = 0;
};

struct EnumLifecycleNormalizationResult {
  bool ok = false;
  int normalized_value = 0;
  bool downgraded_from_unknown = false;
  bool deprecated_value_used = false;
  std::string_view reason = "enum lifecycle normalization failed";
};

struct EnumLifecycleGuardResult {
  bool ok = false;
  std::string_view reason = "enum lifecycle descriptor is invalid";
};

inline EnumLifecycleGuardResult validate_enum_lifecycle_descriptor(
    const EnumLifecycleDescriptor& descriptor) {
  if (descriptor.known_values == nullptr || descriptor.known_value_count == 0U) {
    return EnumLifecycleGuardResult{.ok = false,
                                    .reason = "known_values must not be empty"};
  }

  if (!has_unspecified_enum_sentinel(descriptor.known_values,
                                     descriptor.known_value_count,
                                     descriptor.unspecified_value)) {
    return EnumLifecycleGuardResult{.ok = false,
                                    .reason = "Unspecified sentinel must be present in known_values"};
  }

  if (descriptor.deprecated_values != nullptr) {
    for (std::size_t i = 0; i < descriptor.deprecated_value_count; ++i) {
      if (!is_known_enum_value(descriptor.deprecated_values[i],
                               descriptor.known_values,
                               descriptor.known_value_count)) {
        return EnumLifecycleGuardResult{.ok = false,
                                        .reason = "deprecated value must exist in known_values"};
      }
    }
  }

  return EnumLifecycleGuardResult{.ok = true,
                                  .reason = "enum lifecycle descriptor is valid"};
}

// Applies B011 lifecycle normalization semantics:
// 1) known non-deprecated values are preserved.
// 2) unknown values are downgraded to Unspecified.
// 3) deprecated values remain readable but are marked as deprecated usage.
inline EnumLifecycleNormalizationResult normalize_enum_with_lifecycle(
    int raw_value,
    const EnumLifecycleDescriptor& descriptor) {
  const auto guard = validate_enum_lifecycle_descriptor(descriptor);
  if (!guard.ok) {
    return EnumLifecycleNormalizationResult{.ok = false,
                                            .normalized_value = descriptor.unspecified_value,
                                            .reason = guard.reason};
  }

  if (is_known_enum_value(raw_value, descriptor.known_values, descriptor.known_value_count)) {
    bool deprecated = false;
    if (descriptor.deprecated_values != nullptr) {
      deprecated = is_known_enum_value(raw_value,
                                       descriptor.deprecated_values,
                                       descriptor.deprecated_value_count);
    }

    return EnumLifecycleNormalizationResult{
        .ok = true,
        .normalized_value = raw_value,
        .downgraded_from_unknown = false,
        .deprecated_value_used = deprecated,
        .reason = deprecated ? "known deprecated value preserved for compatibility"
                             : "known active value preserved",
    };
  }

  return EnumLifecycleNormalizationResult{
      .ok = true,
      .normalized_value = descriptor.unspecified_value,
      .downgraded_from_unknown = true,
      .deprecated_value_used = false,
      .reason = "unknown value downgraded to Unspecified",
  };
}

}  // namespace dasall::contracts

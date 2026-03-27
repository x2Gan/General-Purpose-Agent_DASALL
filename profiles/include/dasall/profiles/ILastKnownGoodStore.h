#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "dasall/profiles/ProfileError.h"
#include "dasall/profiles/RuntimePolicySnapshot.h"

namespace dasall::profiles {

using RuntimePolicySnapshotRef = std::shared_ptr<const RuntimePolicySnapshot>;

struct LastKnownGoodSaveResult {
  bool saved = false;
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return saved && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    return saved != error_code.has_value();
  }
};

struct LastKnownGoodLoadResult {
  RuntimePolicySnapshotRef snapshot;
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return static_cast<bool>(snapshot) && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!snapshot) {
      return error_code.has_value();
    }

    return !error_code.has_value() && snapshot->has_consistent_values();
  }
};

class ILastKnownGoodStore {
 public:
  virtual ~ILastKnownGoodStore() = default;

  // Only validated snapshots are allowed to enter the last-known-good store.
  virtual LastKnownGoodSaveResult save(RuntimePolicySnapshotRef snapshot_ref) = 0;
  virtual LastKnownGoodLoadResult load(std::string_view profile_id) const = 0;
};

}  // namespace dasall::profiles
#include "LastKnownGoodStore.h"

namespace dasall::profiles {

LastKnownGoodSaveResult LastKnownGoodStore::save(RuntimePolicySnapshotRef snapshot_ref) {
  if (!snapshot_ref || !snapshot_ref->has_consistent_values()) {
    return LastKnownGoodSaveResult{
        .saved = false,
        .error_code = ProfileErrorCode::LastKnownGoodUnavailable,
    };
  }

  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  snapshots_[snapshot_ref->effective_profile_id()] = std::move(snapshot_ref);

  return LastKnownGoodSaveResult{
      .saved = true,
      .error_code = std::nullopt,
  };
}

LastKnownGoodLoadResult LastKnownGoodStore::load(std::string_view profile_id) const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  const auto it = snapshots_.find(std::string(profile_id));
  if (it == snapshots_.end()) {
    return LastKnownGoodLoadResult{
        .snapshot = nullptr,
        .error_code = ProfileErrorCode::LastKnownGoodUnavailable,
    };
  }

  return LastKnownGoodLoadResult{
      .snapshot = it->second,
      .error_code = std::nullopt,
  };
}

}  // namespace dasall::profiles

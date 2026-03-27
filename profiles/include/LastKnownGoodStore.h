#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "ILastKnownGoodStore.h"

namespace dasall::profiles {

class LastKnownGoodStore final : public ILastKnownGoodStore {
 public:
  LastKnownGoodSaveResult save(RuntimePolicySnapshotRef snapshot_ref) override;
  LastKnownGoodLoadResult load(std::string_view profile_id) const override;

 private:
  mutable std::mutex snapshots_mutex_;
  std::unordered_map<std::string, RuntimePolicySnapshotRef> snapshots_;
};

}  // namespace dasall::profiles

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "ILastKnownGoodStore.h"
#include "IProfileCatalog.h"
#include "RuntimePolicySnapshot.h"

namespace dasall::profiles {

struct RuntimePolicyLoadRequest {
  std::string profile_id;

  [[nodiscard]] bool has_consistent_values() const {
    return !profile_id.empty();
  }
};

struct RuntimePolicyLoadResult {
  std::shared_ptr<const RuntimePolicySnapshot> snapshot;
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

struct RuntimePolicyActivateRequest {
  std::shared_ptr<const RuntimePolicySnapshot> snapshot;
};

struct RuntimePolicyActivateResult {
  std::uint64_t activated_generation = 0U;
  std::optional<ProfileErrorCode> error_code;

  [[nodiscard]] bool ok() const {
    return activated_generation > 0U && !error_code.has_value();
  }

  [[nodiscard]] bool has_consistent_values() const {
    if (!ok()) {
      return error_code.has_value();
    }

    return true;
  }
};

class RuntimePolicyProvider {
 public:
  explicit RuntimePolicyProvider(const IProfileCatalog& catalog);
  RuntimePolicyProvider(const IProfileCatalog& catalog,
                        std::shared_ptr<ILastKnownGoodStore> lkg_store);

  [[nodiscard]] RuntimePolicyLoadResult load_snapshot(const RuntimePolicyLoadRequest& request) const;
  [[nodiscard]] RuntimePolicyActivateResult activate_snapshot(
      const RuntimePolicyActivateRequest& request);
  [[nodiscard]] std::shared_ptr<const RuntimePolicySnapshot> active_snapshot() const;

 private:
  [[nodiscard]] RuntimePolicyLoadResult load_from_last_known_good(
      const std::string& profile_id) const;

  const IProfileCatalog& catalog_;
  std::shared_ptr<ILastKnownGoodStore> lkg_store_;

  mutable std::mutex active_snapshot_mutex_;
  std::shared_ptr<const RuntimePolicySnapshot> active_snapshot_;
};

}  // namespace dasall::profiles

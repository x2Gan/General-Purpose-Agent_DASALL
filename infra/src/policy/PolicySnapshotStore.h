#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "policy/IPolicySnapshotStore.h"

namespace dasall::infra::policy {

struct PolicySnapshotStoreOptions {
  std::size_t max_history_snapshots = 16;
};

class PolicySnapshotStore final : public IPolicySnapshotStore {
 public:
  explicit PolicySnapshotStore(PolicySnapshotStoreOptions options = {});

  [[nodiscard]] PolicyOpResult commit(const PolicySnapshot& snapshot) override;
  [[nodiscard]] PolicySnapshot current() const override;
  [[nodiscard]] PolicySnapshot last_known_good() const override;
  [[nodiscard]] PolicySnapshot get_by_id(const std::string& snapshot_id) const override;

  void inject_commit_failure_for_test(std::string reason);

 private:
  [[nodiscard]] PolicySnapshot normalize_snapshot_locked(const PolicySnapshot& snapshot) const;
  void trim_history_locked();

  PolicySnapshotStoreOptions options_;
  mutable std::mutex snapshots_mutex_;
  std::map<std::string, PolicySnapshot> snapshots_by_id_;
  std::deque<std::string> history_order_;
  PolicySnapshot current_;
  PolicySnapshot last_known_good_;
  std::optional<std::string> pending_commit_failure_reason_;
};

}  // namespace dasall::infra::policy
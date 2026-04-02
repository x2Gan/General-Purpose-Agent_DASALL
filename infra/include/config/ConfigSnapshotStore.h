#pragma once

#include <map>
#include <mutex>
#include <optional>

#include "config/IConfigSnapshotStore.h"

namespace dasall::infra::config {

class ConfigSnapshotStore final : public IConfigSnapshotStore {
 public:
  ConfigSnapshotCommitResult commit(const ConfigSnapshot& snapshot) override;
  [[nodiscard]] std::optional<ConfigSnapshot> get_current() const override;
  [[nodiscard]] std::optional<ConfigSnapshot> get_by_version(std::uint64_t version) const override;
  [[nodiscard]] std::optional<ConfigSnapshot> get_last_known_good() const override;

 private:
  mutable std::mutex snapshots_mutex_;
  std::map<std::uint64_t, ConfigSnapshot> snapshots_by_version_;
  std::optional<ConfigSnapshot> current_;
  std::optional<ConfigSnapshot> last_known_good_;
};

}  // namespace dasall::infra::config
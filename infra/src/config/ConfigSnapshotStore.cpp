#include "config/ConfigSnapshotStore.h"

#include <utility>

#include "config/ConfigErrors.h"

namespace dasall::infra::config {
namespace {

[[nodiscard]] ConfigSnapshotCommitResult make_failure(ConfigErrorCode code,
                                                      std::string message,
                                                      std::string stage,
                                                      std::string source_ref) {
  const ConfigErrorMapping mapping = map_config_error_code(code);
  return ConfigSnapshotCommitResult::failure(mapping.result_code,
                                             std::string(config_error_code_name(code)) + ": " +
                                                 std::move(message),
                                             std::move(stage),
                                             std::move(source_ref));
}

}  // namespace

ConfigSnapshotCommitResult ConfigSnapshotStore::commit(const ConfigSnapshot& snapshot) {
  if (!snapshot.is_valid()) {
    return make_failure(ConfigErrorCode::InvalidSchema,
                        "config snapshot must satisfy the frozen snapshot contract before commit",
                        "config.commit_snapshot",
                        "ConfigSnapshotStore");
  }

  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  if (current_.has_value() && snapshot.version <= current_->version) {
    return make_failure(ConfigErrorCode::Conflict,
                        "config snapshot versions must advance monotonically",
                        "config.commit_snapshot",
                        snapshot.checksum);
  }

  snapshots_by_version_[snapshot.version] = snapshot;
  current_ = snapshot;
  last_known_good_ = snapshot;
  return ConfigSnapshotCommitResult::success(snapshot.version, snapshot.version);
}

std::optional<ConfigSnapshot> ConfigSnapshotStore::get_current() const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  return current_;
}

std::optional<ConfigSnapshot> ConfigSnapshotStore::get_by_version(std::uint64_t version) const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  const auto snapshot = snapshots_by_version_.find(version);
  if (snapshot == snapshots_by_version_.end()) {
    return std::nullopt;
  }

  return snapshot->second;
}

std::optional<ConfigSnapshot> ConfigSnapshotStore::get_last_known_good() const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  return last_known_good_;
}

}  // namespace dasall::infra::config
#include "PolicySnapshotStore.h"

#include <utility>

#include "policy/PolicyErrors.h"

namespace dasall::infra::policy {
namespace {

[[nodiscard]] std::size_t normalize_max_history_snapshots(std::size_t max_history_snapshots) {
  return max_history_snapshots == 0 ? 1 : max_history_snapshots;
}

[[nodiscard]] PolicyOpResult make_failure(PolicyErrorCode code,
                                          std::string message,
                                          std::string stage,
                                          std::string source_ref) {
  const PolicyErrorMapping mapping = map_policy_error_code(code);
  return PolicyOpResult::failure(mapping.result_code,
                                 std::string(policy_error_code_name(code)) + ": " +
                                     std::move(message),
                                 std::move(stage),
                                 std::move(source_ref));
}

}  // namespace

PolicySnapshotStore::PolicySnapshotStore(PolicySnapshotStoreOptions options)
    : options_{.max_history_snapshots =
                   normalize_max_history_snapshots(options.max_history_snapshots)} {}

PolicyOpResult PolicySnapshotStore::commit(const PolicySnapshot& snapshot) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  PolicySnapshot normalized_snapshot = normalize_snapshot_locked(snapshot);
  const std::string source_ref = normalized_snapshot.snapshot_id.empty()
                                     ? std::string("PolicySnapshotStore")
                                     : normalized_snapshot.snapshot_id;

  if (!normalized_snapshot.is_valid()) {
    return make_failure(PolicyErrorCode::BundleInvalid,
                        "policy snapshot must satisfy the frozen snapshot contract before commit",
                        "policy.commit_snapshot",
                        source_ref);
  }

  if (current_.is_valid() && normalized_snapshot.generation <= current_.generation) {
    return make_failure(PolicyErrorCode::StoreCommitFailed,
                        "policy snapshot generation must increase monotonically",
                        "policy.commit_snapshot",
                        source_ref);
  }

  if (snapshots_by_id_.contains(normalized_snapshot.snapshot_id)) {
    return make_failure(PolicyErrorCode::StoreCommitFailed,
                        "policy snapshot id must remain unique across committed history",
                        "policy.commit_snapshot",
                        source_ref);
  }

  if (pending_commit_failure_reason_.has_value()) {
    std::string reason = *pending_commit_failure_reason_;
    pending_commit_failure_reason_.reset();
    return make_failure(PolicyErrorCode::StoreCommitFailed,
                        std::move(reason),
                        "policy.commit_snapshot",
                        source_ref);
  }

  snapshots_by_id_[normalized_snapshot.snapshot_id] = normalized_snapshot;
  history_order_.push_back(normalized_snapshot.snapshot_id);
  current_ = normalized_snapshot;
  last_known_good_ = normalized_snapshot;
  trim_history_locked();

  return PolicyOpResult::success(normalized_snapshot.snapshot_id,
                                 normalized_snapshot.generation);
}

PolicySnapshot PolicySnapshotStore::current() const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  return current_;
}

PolicySnapshot PolicySnapshotStore::last_known_good() const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  return last_known_good_;
}

PolicySnapshot PolicySnapshotStore::get_by_id(const std::string& snapshot_id) const {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  const auto snapshot = snapshots_by_id_.find(snapshot_id);
  if (snapshot == snapshots_by_id_.end()) {
    return PolicySnapshot{};
  }

  return snapshot->second;
}

void PolicySnapshotStore::inject_commit_failure_for_test(std::string reason) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  pending_commit_failure_reason_ =
      reason.empty() ? std::string("simulated policy snapshot store commit failure")
                     : std::move(reason);
}

PolicySnapshot PolicySnapshotStore::normalize_snapshot_locked(
    const PolicySnapshot& snapshot) const {
  PolicySnapshot normalized_snapshot = snapshot;
  if (normalized_snapshot.last_known_good_ref.empty() && last_known_good_.is_valid()) {
    normalized_snapshot.last_known_good_ref = last_known_good_.snapshot_id;
  }

  return normalized_snapshot;
}

void PolicySnapshotStore::trim_history_locked() {
  while (history_order_.size() > options_.max_history_snapshots) {
    const std::string evicted_snapshot_id = history_order_.front();
    history_order_.pop_front();
    if (current_.snapshot_id == evicted_snapshot_id ||
        last_known_good_.snapshot_id == evicted_snapshot_id) {
      continue;
    }

    snapshots_by_id_.erase(evicted_snapshot_id);
  }
}

}  // namespace dasall::infra::policy
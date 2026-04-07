#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "diagnostics/DiagnosticsTypes.h"

namespace dasall::infra::diagnostics {

struct SnapshotStoreOptions {
  std::uint32_t retention_days = 7;
  std::size_t max_snapshot_count = 500;
};

struct SnapshotStoreResult {
  bool stored = false;
  std::string snapshot_id;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static SnapshotStoreResult success(std::string snapshot_id);
  [[nodiscard]] static SnapshotStoreResult failure(contracts::ResultCode result_code,
                                                   std::string message,
                                                   std::string stage,
                                                   std::string source_ref);
  [[nodiscard]] bool references_only_contract_error_types() const;
};

class SnapshotStore final {
 public:
  explicit SnapshotStore(SnapshotStoreOptions options = {});

  [[nodiscard]] SnapshotStoreResult store(const DiagnosticsSnapshot& snapshot);
  [[nodiscard]] std::optional<DiagnosticsSnapshot> get(const std::string& snapshot_id);
  [[nodiscard]] bool contains(const std::string& snapshot_id);

  void inject_commit_failure_for_test(std::string reason);
  void inject_current_time_for_test(std::string now_rfc3339);

 private:
  [[nodiscard]] std::optional<std::int64_t> resolve_current_epoch_seconds_locked() const;
  [[nodiscard]] bool should_expire_locked(const DiagnosticsSnapshot& snapshot,
                                          std::int64_t now_epoch_seconds) const;
  void prune_locked(std::int64_t now_epoch_seconds);

  SnapshotStoreOptions options_{};
  std::mutex snapshots_mutex_;
  std::unordered_map<std::string, DiagnosticsSnapshot> snapshots_by_id_;
  std::deque<std::string> history_order_;
  std::optional<std::string> pending_commit_failure_reason_;
  std::optional<std::string> current_time_override_;
};

}  // namespace dasall::infra::diagnostics
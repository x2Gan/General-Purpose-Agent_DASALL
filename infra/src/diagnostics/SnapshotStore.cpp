#include "diagnostics/SnapshotStore.h"

#include <chrono>
#include <cstdio>
#include <utility>

#include "diagnostics/DiagnosticsErrors.h"

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kSnapshotStoreSourceRef = "SnapshotStore";

[[nodiscard]] std::uint32_t normalize_retention_days(std::uint32_t retention_days) {
  return retention_days == 0 ? 1 : retention_days;
}

[[nodiscard]] std::size_t normalize_max_snapshot_count(std::size_t max_snapshot_count) {
  return max_snapshot_count == 0 ? 1 : max_snapshot_count;
}

[[nodiscard]] bool is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

[[nodiscard]] int days_in_month(int year, int month) {
  switch (month) {
    case 1:
    case 3:
    case 5:
    case 7:
    case 8:
    case 10:
    case 12:
      return 31;
    case 4:
    case 6:
    case 9:
    case 11:
      return 30;
    case 2:
      return is_leap_year(year) ? 29 : 28;
    default:
      return 0;
  }
}

[[nodiscard]] std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
  const unsigned day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(day_of_era) -
         719468;
}

[[nodiscard]] std::optional<std::int64_t> parse_rfc3339_epoch_seconds(std::string_view timestamp) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  std::string buffer(timestamp);
  if (std::sscanf(buffer.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour,
                  &minute, &second) != 6) {
    return std::nullopt;
  }

  if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour < 0 ||
      hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return std::nullopt;
  }

  return days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
         static_cast<std::int64_t>(hour) * 3600 + static_cast<std::int64_t>(minute) * 60 +
         static_cast<std::int64_t>(second);
}

[[nodiscard]] SnapshotStoreResult make_failure(DiagnosticsErrorCode code,
                                               std::string message,
                                               std::string stage,
                                               std::string source_ref) {
  const auto mapping = map_diagnostics_error_code(code);
  return SnapshotStoreResult::failure(
      mapping.result_code,
      std::string(diagnostics_error_code_name(code)) + ": " + std::move(message),
      std::move(stage),
      std::move(source_ref));
}

}  // namespace

SnapshotStoreResult SnapshotStoreResult::success(std::string snapshot_id) {
  return SnapshotStoreResult{
      .stored = true,
      .snapshot_id = std::move(snapshot_id),
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .error = std::nullopt,
  };
}

SnapshotStoreResult SnapshotStoreResult::failure(contracts::ResultCode result_code,
                                                 std::string message,
                                                 std::string stage,
                                                 std::string source_ref) {
  return SnapshotStoreResult{
      .stored = false,
      .snapshot_id = {},
      .result_code = result_code,
      .error = contracts::ErrorInfo{
          .failure_type = contracts::classify_result_code(result_code),
          .retryable = false,
          .safe_to_replan = false,
          .details = contracts::ErrorDetails{
              .code = static_cast<int>(result_code),
              .message = std::move(message),
              .stage = std::move(stage),
          },
          .source_ref = contracts::ErrorSourceRefMinimal{
              .ref_type = "infra.diagnostics",
              .ref_id = std::move(source_ref),
          },
      },
  };
}

bool SnapshotStoreResult::references_only_contract_error_types() const {
  if (!error.has_value()) {
    return stored;
  }

  return error->failure_type.has_value() &&
         *error->failure_type == contracts::classify_result_code(result_code);
}

SnapshotStore::SnapshotStore(SnapshotStoreOptions options)
    : options_{.retention_days = normalize_retention_days(options.retention_days),
               .max_snapshot_count = normalize_max_snapshot_count(options.max_snapshot_count)} {}

SnapshotStoreResult SnapshotStore::store(const DiagnosticsSnapshot& snapshot) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  const std::string source_ref = snapshot.snapshot_id.empty()
                                     ? std::string(kSnapshotStoreSourceRef)
                                     : snapshot.snapshot_id;

  if (!snapshot.is_valid()) {
    return make_failure(DiagnosticsErrorCode::SnapshotStoreFail,
                        "diagnostics snapshot must satisfy the frozen snapshot contract before persistence",
                        "diagnostics.store_snapshot",
                        source_ref);
  }

  if (!parse_rfc3339_epoch_seconds(snapshot.collected_at).has_value()) {
    return make_failure(DiagnosticsErrorCode::SnapshotStoreFail,
                        "diagnostics snapshot collected_at must remain RFC3339 UTC for retention cleanup",
                        "diagnostics.store_snapshot",
                        source_ref);
  }

  if (snapshots_by_id_.contains(snapshot.snapshot_id)) {
    return make_failure(DiagnosticsErrorCode::SnapshotStoreFail,
                        "diagnostics snapshot id must remain unique across retained history",
                        "diagnostics.store_snapshot",
                        source_ref);
  }

  if (pending_commit_failure_reason_.has_value()) {
    std::string reason = *pending_commit_failure_reason_;
    pending_commit_failure_reason_.reset();
    return make_failure(DiagnosticsErrorCode::SnapshotStoreFail,
                        std::move(reason),
                        "diagnostics.store_snapshot",
                        source_ref);
  }

  snapshots_by_id_[snapshot.snapshot_id] = snapshot;
  history_order_.push_back(snapshot.snapshot_id);

  const auto now_epoch_seconds = resolve_current_epoch_seconds_locked();
  if (!now_epoch_seconds.has_value()) {
    snapshots_by_id_.erase(snapshot.snapshot_id);
    history_order_.pop_back();
    return make_failure(DiagnosticsErrorCode::SnapshotStoreFail,
                        "diagnostics snapshot store clock must remain RFC3339 UTC when overridden for tests",
                        "diagnostics.store_snapshot",
                        source_ref);
  }

  prune_locked(*now_epoch_seconds);
  return SnapshotStoreResult::success(snapshot.snapshot_id);
}

std::optional<DiagnosticsSnapshot> SnapshotStore::get(const std::string& snapshot_id) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  const auto now_epoch_seconds = resolve_current_epoch_seconds_locked();
  if (now_epoch_seconds.has_value()) {
    prune_locked(*now_epoch_seconds);
  }

  const auto snapshot = snapshots_by_id_.find(snapshot_id);
  if (snapshot == snapshots_by_id_.end()) {
    return std::nullopt;
  }

  return snapshot->second;
}

bool SnapshotStore::contains(const std::string& snapshot_id) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  const auto now_epoch_seconds = resolve_current_epoch_seconds_locked();
  if (now_epoch_seconds.has_value()) {
    prune_locked(*now_epoch_seconds);
  }

  return snapshots_by_id_.contains(snapshot_id);
}

void SnapshotStore::inject_commit_failure_for_test(std::string reason) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  pending_commit_failure_reason_ =
      reason.empty() ? std::string("simulated diagnostics snapshot store failure")
                     : std::move(reason);
}

void SnapshotStore::inject_current_time_for_test(std::string now_rfc3339) {
  std::lock_guard<std::mutex> lock(snapshots_mutex_);
  current_time_override_ = std::move(now_rfc3339);
}

std::optional<std::int64_t> SnapshotStore::resolve_current_epoch_seconds_locked() const {
  if (current_time_override_.has_value()) {
    return parse_rfc3339_epoch_seconds(*current_time_override_);
  }

  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool SnapshotStore::should_expire_locked(const DiagnosticsSnapshot& snapshot,
                                         std::int64_t now_epoch_seconds) const {
  const auto snapshot_epoch_seconds = parse_rfc3339_epoch_seconds(snapshot.collected_at);
  if (!snapshot_epoch_seconds.has_value()) {
    return true;
  }

  const std::int64_t retention_window_seconds =
      static_cast<std::int64_t>(options_.retention_days) * 86400;
  return *snapshot_epoch_seconds < now_epoch_seconds - retention_window_seconds;
}

void SnapshotStore::prune_locked(std::int64_t now_epoch_seconds) {
  auto iterator = history_order_.begin();
  while (iterator != history_order_.end()) {
    const auto snapshot = snapshots_by_id_.find(*iterator);
    if (snapshot == snapshots_by_id_.end()) {
      iterator = history_order_.erase(iterator);
      continue;
    }

    if (!should_expire_locked(snapshot->second, now_epoch_seconds)) {
      ++iterator;
      continue;
    }

    snapshots_by_id_.erase(snapshot);
    iterator = history_order_.erase(iterator);
  }

  while (history_order_.size() > options_.max_snapshot_count) {
    const std::string evicted_snapshot_id = history_order_.front();
    history_order_.pop_front();
    snapshots_by_id_.erase(evicted_snapshot_id);
  }
}

}  // namespace dasall::infra::diagnostics
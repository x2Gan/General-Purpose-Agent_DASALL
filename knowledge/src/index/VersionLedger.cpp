#include "index/VersionLedger.h"

#include <algorithm>
#include <exception>

namespace dasall::knowledge::index {

namespace {

template <typename EntryContainer>
auto find_entry(EntryContainer& entries, std::string_view snapshot_id) {
  return std::find_if(entries.begin(), entries.end(), [snapshot_id](const VersionLedgerEntry& entry) {
    return entry.snapshot_id == snapshot_id;
  });
}

[[nodiscard]] bool participates_in_last_known_good(const VersionLedgerEntry& entry) {
  if (entry.state == SnapshotState::Active) {
    return true;
  }

  return entry.state == SnapshotState::Superseded && entry.rollback_eligible;
}

[[nodiscard]] bool is_better_last_known_good_candidate(const VersionLedgerEntry& candidate,
                                                       const VersionLedgerEntry& incumbent) {
  if (candidate.activated_at != incumbent.activated_at) {
    return candidate.activated_at > incumbent.activated_at;
  }

  if (candidate.state != incumbent.state) {
    return candidate.state == SnapshotState::Active;
  }

  if (candidate.built_at != incumbent.built_at) {
    return candidate.built_at > incumbent.built_at;
  }

  return candidate.snapshot_id > incumbent.snapshot_id;
}

}  // namespace

bool VersionLedgerEntry::has_consistent_values() const {
  if (snapshot_id.empty() || batch_id.empty() || built_at <= 0 || checksum.empty()) {
    return false;
  }

  if (!parent_snapshot_id.empty() && parent_snapshot_id == snapshot_id) {
    return false;
  }

  if (document_count == 0U && chunk_count != 0U) {
    return false;
  }

  switch (state) {
    case SnapshotState::Pending:
      return activated_at == 0 && !rollback_eligible;
    case SnapshotState::Active:
      return activated_at >= built_at && rollback_eligible;
    case SnapshotState::Superseded:
      return activated_at >= built_at;
  }

  return false;
}

VersionLedger::VersionLedger() = default;

VersionLedger::VersionLedger(VersionLedgerDeps deps)
    : deps_(std::move(deps)) {}

bool VersionLedger::record_candidate(const VersionLedgerEntry& entry) {
  if (!entry.has_consistent_values() || entry.state != SnapshotState::Pending ||
      entry.activated_at != 0 || entry.rollback_eligible) {
    return false;
  }

  if (find_entry(entries_, entry.snapshot_id) != entries_.end()) {
    return false;
  }

  if (!entry.parent_snapshot_id.empty() &&
      find_entry(entries_, entry.parent_snapshot_id) == entries_.end()) {
    return false;
  }

  entries_.push_back(entry);
  return true;
}

bool VersionLedger::mark_active(std::string_view snapshot_id, std::int64_t activated_at) {
  if (snapshot_id.empty() || activated_at <= 0) {
    return false;
  }

  auto target_entry = find_entry(entries_, snapshot_id);
  if (target_entry == entries_.end() || target_entry->state != SnapshotState::Pending ||
      activated_at < target_entry->built_at) {
    return false;
  }

  for (auto& entry : entries_) {
    if (entry.state == SnapshotState::Active) {
      entry.state = SnapshotState::Superseded;
      entry.rollback_eligible = true;
    }
  }

  target_entry->state = SnapshotState::Active;
  target_entry->activated_at = activated_at;
  target_entry->rollback_eligible = true;
  return target_entry->has_consistent_values();
}

bool VersionLedger::mark_superseded(std::string_view snapshot_id) {
  if (snapshot_id.empty()) {
    return false;
  }

  auto target_entry = find_entry(entries_, snapshot_id);
  if (target_entry == entries_.end() || target_entry->state == SnapshotState::Pending) {
    return false;
  }

  if (target_entry->state == SnapshotState::Superseded) {
    return target_entry->has_consistent_values();
  }

  target_entry->state = SnapshotState::Superseded;
  target_entry->rollback_eligible = true;
  return target_entry->has_consistent_values();
}

std::optional<VersionLedgerEntry> VersionLedger::last_known_good() const {
  std::optional<VersionLedgerEntry> best_entry;

  for (const auto& entry : entries_) {
    if (!participates_in_last_known_good(entry) || !checksum_matches(entry)) {
      continue;
    }

    if (!best_entry.has_value() || is_better_last_known_good_candidate(entry, *best_entry)) {
      best_entry = entry;
    }
  }

  return best_entry;
}

bool VersionLedger::checksum_matches(const VersionLedgerEntry& entry) const {
  if (entry.checksum.empty()) {
    return false;
  }

  if (!deps_.read_snapshot_checksum) {
    return true;
  }

  try {
    const auto actual_checksum = deps_.read_snapshot_checksum(entry.snapshot_id);
    return actual_checksum.has_value() && *actual_checksum == entry.checksum;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace dasall::knowledge::index
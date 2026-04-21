#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "index/VersionLedger.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::index::SnapshotState;
using dasall::knowledge::index::VersionLedger;
using dasall::knowledge::index::VersionLedgerDeps;
using dasall::knowledge::index::VersionLedgerEntry;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] VersionLedgerEntry make_candidate(std::string snapshot_id,
                                                std::string parent_snapshot_id,
                                                std::string batch_id,
                                                std::int64_t built_at,
                                                std::string checksum) {
  VersionLedgerEntry entry;
  entry.snapshot_id = std::move(snapshot_id);
  entry.parent_snapshot_id = std::move(parent_snapshot_id);
  entry.batch_id = std::move(batch_id);
  entry.built_at = built_at;
  entry.state = SnapshotState::Pending;
  entry.document_count = 1U;
  entry.chunk_count = 1U;
  entry.checksum = std::move(checksum);
  return entry;
}

void test_version_ledger_limits_rollback_targets_to_activated_snapshots() {
  std::map<std::string, std::string, std::less<>> snapshot_checksums = {
      {"snapshot-001", "checksum-001"},
      {"snapshot-002", "checksum-002"},
  };

  VersionLedger ledger(VersionLedgerDeps{
      .read_snapshot_checksum = [&snapshot_checksums](std::string_view snapshot_id)
          -> std::optional<std::string> {
        const auto checksum_it = snapshot_checksums.find(snapshot_id);
        if (checksum_it == snapshot_checksums.end()) {
          return std::nullopt;
        }

        return checksum_it->second;
      },
  });

  assert_true(ledger.record_candidate(
                  make_candidate("snapshot-001", "", "batch-001", 100, "checksum-001")),
              "first candidate should be recorded successfully");
  assert_true(ledger.mark_active("snapshot-001", 120),
              "first candidate should become the active snapshot");
  assert_true(ledger.record_candidate(make_candidate("snapshot-002", "snapshot-001", "batch-002",
                                                     200, "checksum-002")),
              "second snapshot should remain pending until swap succeeds");

  auto current_good = ledger.last_known_good();
  assert_true(current_good.has_value(),
              "pending snapshots should not displace the current last-known-good target");
  assert_equal("snapshot-001", current_good->snapshot_id,
               "last-known-good should ignore pending candidates");

  assert_true(!ledger.mark_superseded("snapshot-002"),
              "pending candidates must not be marked superseded before activation");
  assert_true(ledger.mark_superseded("snapshot-001"),
              "activated snapshots may be explicitly marked superseded for rollback tracking");

  current_good = ledger.last_known_good();
  assert_true(current_good.has_value(),
              "superseded rollback target should remain discoverable while checksum matches");
  assert_equal("snapshot-001", current_good->snapshot_id,
               "explicitly superseded active snapshot should remain the last-known-good target");
  assert_true(current_good->state == SnapshotState::Superseded,
              "explicit supersede should change the snapshot state to superseded");
  assert_true(current_good->rollback_eligible,
              "explicitly superseded active snapshot should stay rollback eligible");

  snapshot_checksums["snapshot-001"] = "checksum-mismatch";
  assert_true(!ledger.last_known_good().has_value(),
              "checksum mismatch should reject superseded rollback targets fail-closed");
}

}  // namespace

int main() {
  try {
    test_version_ledger_limits_rollback_targets_to_activated_snapshots();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
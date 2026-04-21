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
  entry.document_count = 2U;
  entry.chunk_count = 4U;
  entry.checksum = std::move(checksum);
  return entry;
}

void test_version_ledger_marks_active_and_falls_back_to_previous_good_snapshot() {
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
              "first candidate should be recorded before activation");
  assert_true(!ledger.mark_active("snapshot-001", 90),
              "activation timestamp earlier than built_at should be rejected");
  assert_true(ledger.mark_active("snapshot-001", 120),
              "pending snapshot should become active after successful activation");

  auto current_good = ledger.last_known_good();
  assert_true(current_good.has_value(),
              "active snapshot with matching checksum should be considered last-known-good");
  assert_equal("snapshot-001", current_good->snapshot_id,
               "first activated snapshot should become the initial last-known-good target");
  assert_true(current_good->state == SnapshotState::Active,
              "first activated snapshot should remain active until superseded");
  assert_true(current_good->rollback_eligible,
              "active snapshots should be marked rollback eligible for recovery fallback");

  assert_true(ledger.record_candidate(make_candidate("snapshot-002", "snapshot-001", "batch-002",
                                                     200, "checksum-002")),
              "second snapshot should record lineage to the previous snapshot");
  assert_true(ledger.mark_active("snapshot-002", 240),
              "second candidate should become the active snapshot after swap completion");

  current_good = ledger.last_known_good();
  assert_true(current_good.has_value(),
              "new active snapshot should replace the previous last-known-good target");
  assert_equal("snapshot-002", current_good->snapshot_id,
               "most recent active snapshot should win when its checksum matches");
  assert_true(current_good->state == SnapshotState::Active,
              "newly activated snapshot should report active state");

  snapshot_checksums["snapshot-002"] = "checksum-mismatch";

  const auto fallback_good = ledger.last_known_good();
  assert_true(fallback_good.has_value(),
              "checksum mismatch on the current active snapshot should fall back to a rollback target");
  assert_equal("snapshot-001", fallback_good->snapshot_id,
               "previous active snapshot should remain the rollback target after supersede");
  assert_true(fallback_good->state == SnapshotState::Superseded,
              "previous active snapshot should transition to superseded after the new activation");
  assert_true(fallback_good->rollback_eligible,
              "superseded rollback target should remain eligible for last-known-good selection");
}

}  // namespace

int main() {
  try {
    test_version_ledger_marks_active_and_falls_back_to_previous_good_snapshot();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
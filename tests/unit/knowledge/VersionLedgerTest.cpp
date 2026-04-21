#include <exception>
#include <iostream>
#include <string>

#include "index/VersionLedger.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::index::SnapshotState;
using dasall::knowledge::index::VersionLedger;
using dasall::knowledge::index::VersionLedgerEntry;
using dasall::tests::support::assert_true;

[[nodiscard]] VersionLedgerEntry make_candidate(std::string snapshot_id,
                                                std::string parent_snapshot_id,
                                                std::string batch_id,
                                                std::int64_t built_at,
                                                std::string checksum = "checksum-001") {
  VersionLedgerEntry entry;
  entry.snapshot_id = std::move(snapshot_id);
  entry.parent_snapshot_id = std::move(parent_snapshot_id);
  entry.batch_id = std::move(batch_id);
  entry.built_at = built_at;
  entry.state = SnapshotState::Pending;
  entry.document_count = 1U;
  entry.chunk_count = 2U;
  entry.checksum = std::move(checksum);
  entry.rollback_eligible = false;
  return entry;
}

void test_version_ledger_records_only_consistent_candidates() {
  VersionLedger ledger;

  const auto initial_candidate = make_candidate("snapshot-001", "", "batch-001", 100);
  assert_true(initial_candidate.has_consistent_values(),
              "baseline candidate should satisfy entry consistency rules");
  assert_true(ledger.record_candidate(initial_candidate),
              "ledger should accept a consistent initial candidate");
  assert_true(!ledger.record_candidate(initial_candidate),
              "ledger should reject duplicate snapshot ids");

  const auto missing_parent = make_candidate("snapshot-002", "missing-parent", "batch-002", 200,
                                             "checksum-002");
  assert_true(!ledger.record_candidate(missing_parent),
              "ledger should reject candidates whose parent lineage is unknown");

  auto invalid_candidate = make_candidate("", "", "batch-003", 300, "checksum-003");
  assert_true(!invalid_candidate.has_consistent_values(),
              "candidate without snapshot id should fail local consistency validation");
  assert_true(!ledger.record_candidate(invalid_candidate),
              "ledger should reject malformed candidates");

  assert_true(!ledger.last_known_good().has_value(),
              "pending candidates must not be treated as last-known-good snapshots");
}

}  // namespace

int main() {
  try {
    test_version_ledger_records_only_consistent_candidates();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
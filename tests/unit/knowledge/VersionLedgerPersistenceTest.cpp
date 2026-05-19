#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include "index/VersionLedger.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::index::VersionLedger;
using dasall::knowledge::index::VersionLedgerDeps;
using dasall::knowledge::index::VersionLedgerEntry;
using dasall::knowledge::index::SnapshotState;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct TempDirectory {
  explicit TempDirectory(std::string name)
      : path(std::filesystem::temp_directory_path() / std::move(name)) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path path;
};

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << content;
}

[[nodiscard]] VersionLedgerEntry make_pending_entry(std::string snapshot_id,
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
  entry.rollback_eligible = false;
  return entry;
}

void test_version_ledger_restores_active_snapshot_and_falls_back_to_lkg_on_checksum_mismatch() {
  TempDirectory temp_directory("dasall-version-ledger-persistence-test");
  const auto ledger_path = temp_directory.path / "version_ledger.jsonl";

  std::map<std::string, std::string, std::less<>> checksums = {
      {"snapshot-001", "checksum-001"},
      {"snapshot-002", "checksum-002"},
  };

  const auto make_deps = [&]() {
    return VersionLedgerDeps{
        .read_snapshot_checksum = [&checksums](std::string_view snapshot_id) {
          const auto iterator = checksums.find(std::string(snapshot_id));
          if (iterator == checksums.end()) {
            return std::optional<std::string>{};
          }
          return std::optional<std::string>{iterator->second};
        },
        .ledger_path = ledger_path,
    };
  };

  VersionLedger ledger(make_deps());
  assert_true(ledger.record_candidate(
                  make_pending_entry("snapshot-001", "", "batch-001", 100, "checksum-001")),
              "ledger should persist the first candidate entry");
  assert_true(ledger.mark_active("snapshot-001", 120),
              "ledger should activate the first snapshot before restart");
  assert_true(ledger.record_candidate(make_pending_entry(
                  "snapshot-002", "snapshot-001", "batch-002", 200, "checksum-002")),
              "ledger should persist the second candidate entry");
  assert_true(ledger.mark_active("snapshot-002", 220),
              "ledger should activate the second snapshot before restart");

  VersionLedger reloaded_ledger(make_deps());
  const auto active_entry = reloaded_ledger.active();
  assert_true(active_entry.has_value(),
              "reloaded ledger should recover the persisted active snapshot");
  assert_equal("snapshot-002", active_entry->snapshot_id,
               "startup recovery should preserve the newest active snapshot id");

  checksums["snapshot-002"] = "checksum-mismatch";
  VersionLedger reloaded_with_broken_active(make_deps());
  assert_true(!reloaded_with_broken_active.active().has_value(),
              "checksum mismatch must prevent the persisted active snapshot from being trusted");

  const auto last_known_good = reloaded_with_broken_active.last_known_good();
  assert_true(last_known_good.has_value(),
              "ledger should still recover a rollback-eligible superseded snapshot as LKG");
  assert_equal("snapshot-001", last_known_good->snapshot_id,
               "checksum failure should fall back to the prior last-known-good snapshot");
}

void test_version_ledger_fails_closed_on_unknown_persistence_format() {
  TempDirectory temp_directory("dasall-version-ledger-format-test");
  const auto ledger_path = temp_directory.path / "version_ledger.jsonl";

  write_file(ledger_path,
             "{\"ledger_format_version\":99}\n"
             "{\"snapshot_id\":\"snapshot-001\"}\n"
             "{\"ledger_checksum\":\"ignored\"}\n");

  VersionLedger ledger(VersionLedgerDeps{
      .read_snapshot_checksum = [](std::string_view) {
        return std::optional<std::string>{"checksum-001"};
      },
      .ledger_path = ledger_path,
  });

  assert_true(!ledger.active().has_value(),
              "unknown ledger format version must fail closed instead of trusting persisted active state");
  assert_true(!ledger.last_known_good().has_value(),
              "unknown ledger format version must discard persisted last-known-good state");
}

}  // namespace

int main() {
  try {
    test_version_ledger_restores_active_snapshot_and_falls_back_to_lkg_on_checksum_mismatch();
    test_version_ledger_fails_closed_on_unknown_persistence_format();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
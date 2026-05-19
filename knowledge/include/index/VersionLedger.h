#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dasall::knowledge::index {

enum class SnapshotState : std::uint8_t {
  Pending = 0,
  Active = 1,
  Superseded = 2,
};

struct VersionLedgerEntry {
  std::string snapshot_id;
  std::string parent_snapshot_id;
  std::string batch_id;
  std::int64_t built_at = 0;
  std::int64_t activated_at = 0;
  SnapshotState state = SnapshotState::Pending;
  std::size_t document_count = 0U;
  std::size_t chunk_count = 0U;
  std::string checksum;
  bool rollback_eligible = false;

  [[nodiscard]] bool has_consistent_values() const;
};

struct VersionLedgerDeps {
  std::function<std::optional<std::string>(std::string_view snapshot_id)> read_snapshot_checksum;
  std::filesystem::path ledger_path;
};

class VersionLedger {
 public:
  VersionLedger();
  explicit VersionLedger(VersionLedgerDeps deps);

  [[nodiscard]] bool record_candidate(const VersionLedgerEntry& entry);
  [[nodiscard]] bool mark_active(std::string_view snapshot_id, std::int64_t activated_at);
  [[nodiscard]] bool mark_superseded(std::string_view snapshot_id);
  [[nodiscard]] std::optional<VersionLedgerEntry> active() const;
  [[nodiscard]] std::optional<VersionLedgerEntry> last_known_good() const;

 private:
  [[nodiscard]] bool persist_entries() const;
  [[nodiscard]] bool checksum_matches(const VersionLedgerEntry& entry) const;

  std::vector<VersionLedgerEntry> entries_;
  VersionLedgerDeps deps_{};
};

}  // namespace dasall::knowledge::index
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "config/IConfigSnapshotStore.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::config::ConfigSnapshot make_valid_snapshot(std::uint64_t version,
                                                          std::string checksum_suffix) {
  return dasall::infra::config::ConfigSnapshot{
      .version = version,
      .checksum = std::string("sha256:") + std::move(checksum_suffix),
      .created_at = std::string("2026-03-31T11:00:00Z"),
      .data = {dasall::infra::config::TypedConfig{
          .key_path = std::string("infra.config.validation.strict"),
          .value_type = dasall::infra::config::ConfigValueType::Boolean,
          .serialized_value = std::string("true"),
          .schema_version = std::string("1"),
          .source_kind = dasall::infra::config::ConfigSourceKind::Profile,
          .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
          .secret_backed = false,
      }},
      .source_chain = {
          dasall::infra::config::ConfigLayerRef{
              .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
              .document_format = dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
              .version_ref = std::string("defaults@1"),
              .schema_version = std::string("1"),
          },
          dasall::infra::config::ConfigLayerRef{
              .source_kind = dasall::infra::config::ConfigSourceKind::Profile,
              .document_format = dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("profiles/desktop_full/runtime_policy.yaml"),
              .version_ref = std::string("desktop_full@1"),
              .schema_version = std::string("1"),
          },
      },
  };
}

class NullConfigSnapshotStore final : public dasall::infra::config::IConfigSnapshotStore {
 public:
  dasall::infra::config::ConfigSnapshotCommitResult commit(
      const dasall::infra::config::ConfigSnapshot& snapshot) override {
    if (!snapshot.is_valid()) {
      return dasall::infra::config::ConfigSnapshotCommitResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config snapshot must remain valid before commit",
          "config.commit_snapshot",
          "NullConfigSnapshotStore");
    }

    if (current_.has_value() && snapshot.version <= current_->version) {
      return dasall::infra::config::ConfigSnapshotCommitResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config snapshot versions must increase monotonically",
          "config.commit_snapshot",
          "NullConfigSnapshotStore");
    }

    history_.push_back(snapshot);
    current_ = snapshot;
    last_known_good_ = snapshot;
    return dasall::infra::config::ConfigSnapshotCommitResult::success(snapshot.version,
                                                                      snapshot.version);
  }

  [[nodiscard]] std::optional<dasall::infra::config::ConfigSnapshot> get_current() const override {
    return current_;
  }

  [[nodiscard]] std::optional<dasall::infra::config::ConfigSnapshot> get_by_version(
      std::uint64_t version) const override {
    for (const auto& snapshot : history_) {
      if (snapshot.version == version) {
        return snapshot;
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] std::optional<dasall::infra::config::ConfigSnapshot> get_last_known_good() const override {
    return last_known_good_;
  }

 private:
  std::vector<dasall::infra::config::ConfigSnapshot> history_;
  std::optional<dasall::infra::config::ConfigSnapshot> current_;
  std::optional<dasall::infra::config::ConfigSnapshot> last_known_good_;
};

void test_config_snapshot_store_interface_commits_and_reads_versioned_snapshots() {
  using dasall::infra::config::ConfigSnapshot;
  using dasall::infra::config::ConfigSnapshotCommitResult;
  using dasall::infra::config::IConfigSnapshotStore;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<IConfigSnapshotStore&>().commit(
                                   std::declval<const ConfigSnapshot&>())),
                               ConfigSnapshotCommitResult>);
  static_assert(std::is_same_v<decltype(std::declval<const IConfigSnapshotStore&>().get_current()),
                               std::optional<ConfigSnapshot>>);
  static_assert(std::is_same_v<decltype(std::declval<const IConfigSnapshotStore&>().get_by_version(
                                   std::declval<std::uint64_t>())),
                               std::optional<ConfigSnapshot>>);
  static_assert(std::is_same_v<decltype(std::declval<const IConfigSnapshotStore&>().get_last_known_good()),
                               std::optional<ConfigSnapshot>>);

  NullConfigSnapshotStore store;
  const auto v7 = make_valid_snapshot(7, "cfg-snapshot-007");
  const auto v8 = make_valid_snapshot(8, "cfg-snapshot-008");

  const auto commit_v7 = store.commit(v7);
  assert_true(commit_v7.committed && commit_v7.current_version == 7 &&
                  commit_v7.last_known_good_version == 7,
              "IConfigSnapshotStore should expose current and last-known-good versions after the first commit");

  const auto commit_v8 = store.commit(v8);
  assert_true(commit_v8.committed && commit_v8.current_version == 8 &&
                  commit_v8.last_known_good_version == 8,
              "IConfigSnapshotStore should accept monotonically increasing snapshot versions");

  const auto current = store.get_current();
  assert_true(current.has_value() && current->version == 8,
              "snapshot store should return the latest committed snapshot as current");

  const auto by_version = store.get_by_version(7);
  assert_true(by_version.has_value() && by_version->version == 7,
              "snapshot store should keep readable history by explicit version");

  const auto last_known_good = store.get_last_known_good();
  assert_true(last_known_good.has_value() && last_known_good->version == 8,
              "snapshot store should keep the latest valid commit as last-known-good in the minimal interface contract");
}

void test_config_snapshot_store_interface_rejects_invalid_or_non_monotonic_commits() {
  using dasall::tests::support::assert_true;

  NullConfigSnapshotStore store;
  const auto invalid_commit = store.commit(dasall::infra::config::ConfigSnapshot{});
  assert_true(!invalid_commit.committed,
              "IConfigSnapshotStore should reject structurally invalid snapshots before persisting them");
  assert_true(invalid_commit.references_only_contract_error_types(),
              "snapshot store commit failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto v10 = make_valid_snapshot(10, "cfg-snapshot-010");
  const auto v9 = make_valid_snapshot(9, "cfg-snapshot-009");
  assert_true(store.commit(v10).committed,
              "snapshot store should accept the first valid snapshot before testing monotonic version guards");

  const auto non_monotonic_commit = store.commit(v9);
  assert_true(!non_monotonic_commit.committed,
              "IConfigSnapshotStore should reject snapshots whose version does not advance monotonically");
  assert_true(non_monotonic_commit.references_only_contract_error_types(),
              "non-monotonic commit failures should remain inside contracts ResultCode/ErrorInfo types");
  assert_true(!store.get_by_version(99).has_value(),
              "snapshot store should return nullopt when a requested historical version does not exist");
}

}  // namespace

int main() {
  try {
    test_config_snapshot_store_interface_commits_and_reads_versioned_snapshots();
    test_config_snapshot_store_interface_rejects_invalid_or_non_monotonic_commits();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
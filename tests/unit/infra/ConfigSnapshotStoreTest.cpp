#include <exception>
#include <iostream>
#include <string>

#include "config/ConfigErrors.h"
#include "config/ConfigSnapshotStore.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::config::ConfigSnapshot make_snapshot(std::uint64_t version,
                                                                  std::string checksum_suffix) {
  return dasall::infra::config::ConfigSnapshot{
      .version = version,
      .checksum = std::string("sha256:") + std::move(checksum_suffix),
      .created_at = std::string("2026-04-02T00:00:00Z"),
      .data = {
          dasall::infra::config::TypedConfig{
              .key_path = std::string("infra.config.validation.strict"),
              .value_type = dasall::infra::config::ConfigValueType::Boolean,
              .serialized_value = std::string("true"),
              .schema_version = std::string("1"),
              .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
              .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
              .secret_backed = false,
          },
      },
      .source_chain = {
          dasall::infra::config::ConfigLayerRef{
              .source_kind = dasall::infra::config::ConfigSourceKind::Defaults,
              .document_format = dasall::infra::config::ConfigDocumentFormat::RuntimePolicyYamlV1,
              .source_id = std::string("infra/config/defaults/runtime_policy.yaml"),
              .version_ref = std::string("defaults@1"),
              .schema_version = std::string("1"),
          },
      },
  };
}

void test_config_snapshot_store_commits_history_and_last_known_good() {
  using dasall::infra::config::ConfigSnapshotStore;
  using dasall::tests::support::assert_true;

  ConfigSnapshotStore store;
  const auto snapshot_v7 = make_snapshot(7, "cfg-store-007");
  const auto snapshot_v8 = make_snapshot(8, "cfg-store-008");

  const auto commit_v7 = store.commit(snapshot_v7);
  assert_true(commit_v7.committed && commit_v7.current_version == 7 &&
                  commit_v7.last_known_good_version == 7,
              "ConfigSnapshotStore should expose current and last-known-good versions after the first commit");

  const auto commit_v8 = store.commit(snapshot_v8);
  assert_true(commit_v8.committed && commit_v8.current_version == 8 &&
                  commit_v8.last_known_good_version == 8,
              "ConfigSnapshotStore should accept monotonically increasing snapshots and advance current/LKG together");

  const auto current = store.get_current();
  assert_true(current.has_value() && current->version == 8,
              "ConfigSnapshotStore should return the latest committed snapshot as current");

  const auto by_version = store.get_by_version(7);
  assert_true(by_version.has_value() && by_version->checksum == "sha256:cfg-store-007",
              "ConfigSnapshotStore should keep readable snapshot history by version");

  const auto last_known_good = store.get_last_known_good();
  assert_true(last_known_good.has_value() && last_known_good->version == 8,
              "ConfigSnapshotStore should keep the latest successful commit as last-known-good");
}

void test_config_snapshot_store_rejects_invalid_or_non_monotonic_commits_and_preserves_lkg() {
  using dasall::infra::config::ConfigErrorCode;
  using dasall::infra::config::ConfigSnapshotStore;
  using dasall::infra::config::map_config_error_code;
  using dasall::tests::support::assert_true;

  ConfigSnapshotStore store;
  const auto invalid_commit = store.commit(dasall::infra::config::ConfigSnapshot{});
  assert_true(!invalid_commit.committed && invalid_commit.references_only_contract_error_types(),
              "ConfigSnapshotStore should reject invalid snapshots before persisting them");
  assert_true(invalid_commit.result_code ==
                  map_config_error_code(ConfigErrorCode::InvalidSchema).result_code,
              "ConfigSnapshotStore should map invalid snapshot commits to the frozen config validation category");

  const auto snapshot_v10 = make_snapshot(10, "cfg-store-010");
  const auto snapshot_v9 = make_snapshot(9, "cfg-store-009");
  assert_true(store.commit(snapshot_v10).committed,
              "ConfigSnapshotStore should accept a first valid snapshot before exercising monotonic guards");

  const auto non_monotonic_commit = store.commit(snapshot_v9);
  assert_true(!non_monotonic_commit.committed && non_monotonic_commit.references_only_contract_error_types(),
              "ConfigSnapshotStore should reject snapshots whose version does not advance monotonically");
  assert_true(non_monotonic_commit.result_code ==
                  map_config_error_code(ConfigErrorCode::Conflict).result_code,
              "ConfigSnapshotStore should map non-monotonic commits to the frozen config conflict category");

  const auto last_known_good = store.get_last_known_good();
  assert_true(last_known_good.has_value() && last_known_good->version == 10,
              "ConfigSnapshotStore should preserve the previous last-known-good snapshot after a failed commit");
  assert_true(!store.get_by_version(99).has_value(),
              "ConfigSnapshotStore should return nullopt when a requested historical version does not exist");
}

}  // namespace

int main() {
  try {
    test_config_snapshot_store_commits_history_and_last_known_good();
    test_config_snapshot_store_rejects_invalid_or_non_monotonic_commits_and_preserves_lkg();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
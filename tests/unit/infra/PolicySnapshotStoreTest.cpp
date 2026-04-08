#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "policy/PolicyErrors.h"
#include "policy/PolicySnapshotStore.h"
#include "support/TestAssertions.h"

namespace {

dasall::infra::policy::PolicyRuleDescriptor make_rule(std::string rule_id,
                                                      std::string reason_code) {
  return dasall::infra::policy::PolicyRuleDescriptor{
      .rule_id = std::move(rule_id),
      .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
      .subject = std::string("ops"),
      .action = std::string("commit_snapshot"),
      .target_selector = std::string("policy.snapshot"),
      .effect = dasall::infra::policy::PolicyEffect::Deny,
      .priority = 1,
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .conditions = {std::string("ticket_approved")},
      .reason_code = std::move(reason_code),
  };
}

dasall::infra::policy::PolicySnapshot make_snapshot(std::string snapshot_id,
                                                    std::uint64_t generation,
                                                    std::string last_known_good_ref = {}) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::move(snapshot_id),
      .generation = generation,
      .version = std::string("policy-v") + std::to_string(generation),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {make_rule(std::string("policy-store-rule-") +
                                        std::to_string(generation),
                                    "policy_store_guard")},
      .created_at = std::string("2026-04-05T08:30:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::move(last_known_good_ref),
  };
}

void test_policy_snapshot_store_commits_history_and_updates_lkg_linkage() {
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::tests::support::assert_true;

  PolicySnapshotStore store;
  const auto snapshot7 = make_snapshot("snapshot-007", 7);
  const auto snapshot8 = make_snapshot("snapshot-008", 8);

  const auto commit7 = store.commit(snapshot7);
  assert_true(commit7.applied && commit7.snapshot_id == "snapshot-007" && commit7.generation == 7,
              "PolicySnapshotStore should accept the first valid snapshot commit");

  const auto commit8 = store.commit(snapshot8);
  assert_true(commit8.applied && commit8.snapshot_id == "snapshot-008" && commit8.generation == 8,
              "PolicySnapshotStore should advance current generation on the next valid commit");

  const auto current = store.current();
  assert_true(current.is_valid() && current.snapshot_id == "snapshot-008" &&
                  current.last_known_good_ref == "snapshot-007",
              "PolicySnapshotStore should stamp the previous successful snapshot as rollback linkage when callers omit last_known_good_ref");

  const auto historical = store.get_by_id("snapshot-007");
  assert_true(historical.is_valid() && historical.snapshot_id == "snapshot-007" &&
                  historical.generation == 7,
              "PolicySnapshotStore should keep committed history addressable by snapshot id");

  const auto last_known_good = store.last_known_good();
  assert_true(last_known_good.is_valid() && last_known_good.snapshot_id == "snapshot-008" &&
                  last_known_good.generation == 8,
              "PolicySnapshotStore should expose the latest successful snapshot as last-known-good");
}

void test_policy_snapshot_store_rejects_invalid_and_non_monotonic_commits() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySnapshotStore store;
  const auto invalid_commit = store.commit(dasall::infra::policy::PolicySnapshot{});
  assert_true(!invalid_commit.applied && invalid_commit.references_only_contract_error_types(),
              "PolicySnapshotStore should reject structurally invalid snapshots before persisting them");
  assert_true(invalid_commit.result_code ==
                  map_policy_error_code(PolicyErrorCode::BundleInvalid).result_code,
              "PolicySnapshotStore should map invalid snapshot commits to the frozen policy validation category");

  const auto snapshot10 = make_snapshot("snapshot-010", 10);
  assert_true(store.commit(snapshot10).applied,
              "PolicySnapshotStore should accept a first valid snapshot before exercising monotonic guards");

  const auto non_monotonic_commit = store.commit(make_snapshot("snapshot-009", 9));
  assert_true(!non_monotonic_commit.applied &&
                  non_monotonic_commit.references_only_contract_error_types(),
              "PolicySnapshotStore should reject snapshots whose generation does not advance monotonically");
  assert_true(non_monotonic_commit.result_code ==
                  map_policy_error_code(PolicyErrorCode::StoreCommitFailed).result_code,
              "PolicySnapshotStore should map non-monotonic commits to the frozen store-commit failure category");
  assert_true(store.current().snapshot_id == "snapshot-010" &&
                  store.last_known_good().snapshot_id == "snapshot-010" &&
                  !store.get_by_id("snapshot-009").is_valid(),
              "PolicySnapshotStore should preserve current/LKG and avoid recording history when commit fails");
}

void test_policy_snapshot_store_trims_history_and_preserves_state_on_injected_commit_failure() {
  using dasall::infra::policy::PolicyErrorCode;
  using dasall::infra::policy::PolicySnapshotStore;
  using dasall::infra::policy::PolicySnapshotStoreOptions;
  using dasall::infra::policy::map_policy_error_code;
  using dasall::tests::support::assert_true;

  PolicySnapshotStore store(PolicySnapshotStoreOptions{.max_history_snapshots = 2});
  assert_true(store.commit(make_snapshot("snapshot-011", 11)).applied,
              "PolicySnapshotStore should accept the first snapshot in a bounded history store");
  assert_true(store.commit(make_snapshot("snapshot-012", 12)).applied,
              "PolicySnapshotStore should accept the second snapshot in a bounded history store");
  assert_true(store.commit(make_snapshot("snapshot-013", 13)).applied,
              "PolicySnapshotStore should accept the third snapshot before exercising trim and injected failure paths");

  assert_true(!store.get_by_id("snapshot-011").is_valid() &&
                  store.get_by_id("snapshot-012").is_valid() &&
                  store.get_by_id("snapshot-013").is_valid(),
              "PolicySnapshotStore should trim the oldest committed history entry once max_history_snapshots is exceeded");

  store.inject_commit_failure_for_test("simulated persistent store failure");
  const auto failed_commit = store.commit(make_snapshot("snapshot-014", 14));
  assert_true(!failed_commit.applied && failed_commit.references_only_contract_error_types(),
              "PolicySnapshotStore should expose injected commit failures through the frozen PolicyOpResult boundary");
  assert_true(failed_commit.result_code ==
                  map_policy_error_code(PolicyErrorCode::StoreCommitFailed).result_code,
              "PolicySnapshotStore should map injected failures to the frozen store-commit failure category");
  assert_true(failed_commit.error_info.has_value() &&
                  failed_commit.error_info->details.message.find("simulated persistent store failure") !=
                      std::string::npos,
              "PolicySnapshotStore should preserve the injected failure reason for observability");
  assert_true(store.current().snapshot_id == "snapshot-013" &&
                  store.last_known_good().snapshot_id == "snapshot-013" &&
                  !store.get_by_id("snapshot-014").is_valid(),
              "PolicySnapshotStore should keep current/LKG stable and avoid recording history when commit injection forces failure");
}

}  // namespace

int main() {
  try {
    test_policy_snapshot_store_commits_history_and_updates_lkg_linkage();
    test_policy_snapshot_store_rejects_invalid_and_non_monotonic_commits();
    test_policy_snapshot_store_trims_history_and_preserves_state_on_injected_commit_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
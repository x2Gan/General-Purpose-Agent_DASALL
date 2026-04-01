#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "policy/IPolicySnapshotStore.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasLoadFromSourcesMethod = requires {
  &T::load_from_sources;
};

template <typename T>
concept HasValidateBundleMethod = requires {
  &T::validate_bundle;
};

template <typename T>
concept HasLoadPolicyMethod = requires {
  &T::load_policy;
};

template <typename T>
concept HasEvaluateMethod = requires {
  &T::evaluate;
};

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
                                                    std::string last_known_good_ref) {
  return dasall::infra::policy::PolicySnapshot{
      .snapshot_id = std::move(snapshot_id),
      .generation = generation,
      .version = std::string("policy-v") + std::to_string(generation),
      .mode = dasall::infra::policy::PolicyMode::Enforced,
      .effective_rules = {make_rule(std::string("policy-store-rule-") + std::to_string(generation),
                                    "policy_store_guard")},
      .created_at = std::string("2026-04-01T16:10:00Z"),
      .source_chain = {std::string("defaults"), std::string("profile:desktop_full")},
      .last_known_good_ref = std::move(last_known_good_ref),
  };
}

class InMemoryPolicySnapshotStore final : public dasall::infra::policy::IPolicySnapshotStore {
 public:
  [[nodiscard]] dasall::infra::policy::PolicyOpResult commit(
      const dasall::infra::policy::PolicySnapshot& snapshot) override {
    if (!snapshot.is_valid()) {
      return dasall::infra::policy::PolicyOpResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "policy snapshot must remain valid before commit",
          "policy.commit_snapshot",
          "InMemoryPolicySnapshotStore");
    }

    if (current_.is_valid() && snapshot.generation <= current_.generation) {
      return dasall::infra::policy::PolicyOpResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "policy snapshot generation must increase monotonically",
          "policy.commit_snapshot",
          "InMemoryPolicySnapshotStore");
    }

    history_.push_back(snapshot);
    current_ = snapshot;
    last_known_good_ = snapshot;
    return dasall::infra::policy::PolicyOpResult::success(snapshot.snapshot_id,
                                                          snapshot.generation);
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot current() const override {
    return current_;
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot last_known_good() const override {
    return last_known_good_;
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot get_by_id(
      const std::string& snapshot_id) const override {
    for (const auto& snapshot : history_) {
      if (snapshot.snapshot_id == snapshot_id) {
        return snapshot;
      }
    }

    return dasall::infra::policy::PolicySnapshot{};
  }

 private:
  std::vector<dasall::infra::policy::PolicySnapshot> history_;
  dasall::infra::policy::PolicySnapshot current_;
  dasall::infra::policy::PolicySnapshot last_known_good_;
};

void test_policy_snapshot_store_interface_commits_and_reads_generationed_snapshots() {
  using dasall::infra::policy::IPolicySnapshotStore;
  using dasall::infra::policy::PolicyOpResult;
  using dasall::infra::policy::PolicySnapshot;
  using dasall::tests::support::assert_true;

  using CommitSignature = PolicyOpResult (IPolicySnapshotStore::*)(const PolicySnapshot&);
  using CurrentSignature = PolicySnapshot (IPolicySnapshotStore::*)() const;
  using LastKnownGoodSignature = PolicySnapshot (IPolicySnapshotStore::*)() const;
  using GetByIdSignature = PolicySnapshot (IPolicySnapshotStore::*)(const std::string&) const;

  static_assert(std::is_same_v<decltype(&IPolicySnapshotStore::commit), CommitSignature>);
  static_assert(std::is_same_v<decltype(&IPolicySnapshotStore::current), CurrentSignature>);
  static_assert(std::is_same_v<decltype(&IPolicySnapshotStore::last_known_good),
                               LastKnownGoodSignature>);
  static_assert(std::is_same_v<decltype(&IPolicySnapshotStore::get_by_id), GetByIdSignature>);
  static_assert(std::is_abstract_v<IPolicySnapshotStore>);

  InMemoryPolicySnapshotStore store;
  const auto snapshot7 = make_snapshot("snapshot-007", 7, "snapshot-006");
  const auto snapshot8 = make_snapshot("snapshot-008", 8, "snapshot-007");

  const auto commit7 = store.commit(snapshot7);
  assert_true(commit7.applied && commit7.snapshot_id == "snapshot-007" && commit7.generation == 7,
              "IPolicySnapshotStore should expose commit as the frozen operation-result boundary for the first valid snapshot");

  const auto commit8 = store.commit(snapshot8);
  assert_true(commit8.applied && commit8.snapshot_id == "snapshot-008" && commit8.generation == 8,
              "IPolicySnapshotStore should accept monotonically increasing generations on subsequent commits");

  const auto current = store.current();
  assert_true(current.is_valid() && current.snapshot_id == "snapshot-008" && current.generation == 8,
              "policy snapshot store should return the latest committed snapshot as current");

  const auto by_id = store.get_by_id("snapshot-007");
  assert_true(by_id.is_valid() && by_id.snapshot_id == "snapshot-007" && by_id.generation == 7,
              "policy snapshot store should keep readable history by explicit snapshot id");

  const auto last_known_good = store.last_known_good();
  assert_true(last_known_good.is_valid() && last_known_good.snapshot_id == "snapshot-008" &&
                  last_known_good.last_known_good_ref == "snapshot-007",
              "policy snapshot store should keep the latest committed snapshot as last-known-good while preserving rollback linkage");
}

void test_policy_snapshot_store_interface_rejects_invalid_or_non_monotonic_commits() {
  using dasall::infra::policy::IPolicySnapshotStore;
  using dasall::tests::support::assert_true;

  InMemoryPolicySnapshotStore store;
  const auto invalid_commit = store.commit(dasall::infra::policy::PolicySnapshot{});
  assert_true(!invalid_commit.applied,
              "IPolicySnapshotStore should reject structurally invalid snapshots before persisting them");
  assert_true(invalid_commit.references_only_contract_error_types(),
              "snapshot store commit failures should remain inside contracts ResultCode/ErrorInfo types");

  const auto snapshot10 = make_snapshot("snapshot-010", 10, "snapshot-009");
  const auto snapshot9 = make_snapshot("snapshot-009", 9, "snapshot-008");
  assert_true(store.commit(snapshot10).applied,
              "snapshot store should accept the first valid snapshot before testing monotonic generation guards");

  const auto non_monotonic_commit = store.commit(snapshot9);
  assert_true(!non_monotonic_commit.applied,
              "IPolicySnapshotStore should reject snapshots whose generation does not advance monotonically");
  assert_true(non_monotonic_commit.references_only_contract_error_types(),
              "non-monotonic commit failures should remain inside contracts ResultCode/ErrorInfo types");
  assert_true(store.current().snapshot_id == "snapshot-010" &&
                  !store.get_by_id("snapshot-missing").is_valid(),
              "snapshot store should preserve current on commit failure and return an invalid placeholder for a missing snapshot id");

  static_assert(!HasLoadFromSourcesMethod<IPolicySnapshotStore>);
  static_assert(!HasValidateBundleMethod<IPolicySnapshotStore>);
  static_assert(!HasLoadPolicyMethod<IPolicySnapshotStore>);
  static_assert(!HasEvaluateMethod<IPolicySnapshotStore>);

  assert_true(std::has_virtual_destructor_v<IPolicySnapshotStore>,
              "IPolicySnapshotStore should keep a virtual destructor as the only lifecycle requirement of the pure abstract boundary");
  assert_true(!std::is_default_constructible_v<IPolicySnapshotStore>,
              "IPolicySnapshotStore should stay focused on snapshot persistence semantics and should not absorb loader, validator, or manager responsibilities");
}

}  // namespace

int main() {
  try {
    test_policy_snapshot_store_interface_commits_and_reads_generationed_snapshots();
    test_policy_snapshot_store_interface_rejects_invalid_or_non_monotonic_commits();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
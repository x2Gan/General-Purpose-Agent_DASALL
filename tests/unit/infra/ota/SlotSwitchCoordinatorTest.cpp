#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "ota/SlotSwitchCoordinator.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::ota::BootMutationResult;
using dasall::infra::ota::BootSwitchResult;
using dasall::infra::ota::BootTargetQueryResult;
using dasall::infra::ota::IBootControlAdapter;
using dasall::infra::ota::InstallEvidence;
using dasall::infra::ota::IRollbackTokenFactory;
using dasall::infra::ota::ISlotInventoryProvider;
using dasall::infra::ota::RollbackToken;
using dasall::infra::ota::SlotInventory;
using dasall::infra::ota::SlotSwitchCoordinator;
using dasall::infra::ota::SwitchPolicySnapshot;

InstallEvidence make_slot_install_evidence() {
  return InstallEvidence{
      .artifact_id = std::string("artifact-rootfs-010"),
      .written_target = std::string("/dev/mmcblk0p3"),
      .checksum = std::string("sha256:slot-010"),
      .install_ts = std::string("2026-04-07T13:00:00Z"),
      .installer_version = std::string("install-executor/1.0"),
  };
}

class FakeSlotInventoryProvider final : public ISlotInventoryProvider {
 public:
  SlotInventory inventory;
  mutable std::size_t calls = 0;

  [[nodiscard]] SlotInventory describe_slot_group(std::string_view) const override {
    ++calls;
    return inventory;
  }
};

class FakeRollbackTokenFactory final : public IRollbackTokenFactory {
 public:
  RollbackToken token;
  mutable std::size_t calls = 0;

  [[nodiscard]] RollbackToken make_token(
      const dasall::infra::ota::SlotPlan&,
      const std::vector<InstallEvidence>&) const override {
    ++calls;
    return token;
  }
};

class FakeBootControlAdapter final : public IBootControlAdapter {
 public:
  explicit FakeBootControlAdapter(std::string active_target)
      : active_target_(std::move(active_target)) {}

  [[nodiscard]] BootTargetQueryResult get_active_target() const override {
    if (active_target_.empty()) {
      return BootTargetQueryResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "active target must stay queryable before set_next_boot",
          "ota.get_active_target",
          "FakeBootControlAdapter");
    }

    return BootTargetQueryResult::success(active_target_);
  }

  [[nodiscard]] BootMutationResult set_next_boot(std::string_view target) override {
    if (target.empty()) {
      return BootMutationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "next boot target must stay explicit",
          "ota.set_next_boot",
          "FakeBootControlAdapter");
    }

    ++set_calls;
    next_target_ = std::string(target);
    return BootMutationResult::success(next_target_, std::string("set_next_boot"));
  }

  [[nodiscard]] BootMutationResult mark_boot_success(std::string_view target) override {
    return BootMutationResult::success(std::string(target),
                                       std::string("mark_boot_success"));
  }

  [[nodiscard]] BootMutationResult mark_boot_failed(std::string_view target) override {
    return BootMutationResult::success(std::string(target),
                                       std::string("mark_boot_failed"));
  }

  [[nodiscard]] const std::string& next_target() const { return next_target_; }
  [[nodiscard]] std::size_t mutation_calls() const { return set_calls; }
  void set_active_target(std::string active_target) { active_target_ = std::move(active_target); }

 private:
  std::string active_target_;
  std::string next_target_;
  std::size_t set_calls = 0;
};

void test_slot_switch_coordinator_builds_inactive_slot_plan_and_token_before_switch() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  FakeSlotInventoryProvider inventory_provider;
  inventory_provider.inventory = SlotInventory{
      .slot_group = std::string("rootfs"),
      .active_slot = std::string("rootfs_a"),
      .candidate_slots = {std::string("rootfs_a"), std::string("rootfs_b")},
  };
  FakeRollbackTokenFactory token_factory;
  token_factory.token = RollbackToken{
      .rollback_id = std::string("rollback-010"),
      .previous_boot_target = std::string("rootfs_a"),
      .staged_artifacts = {std::string("artifact-rootfs-010")},
      .created_at = std::string("2026-04-07T13:01:00Z"),
      .expires_at = std::string("2026-04-08T13:01:00Z"),
  };
  FakeBootControlAdapter boot_control_adapter("rootfs_a");

  SlotSwitchCoordinator coordinator(SlotSwitchCoordinator::Dependencies{
      .slot_inventory_provider = &inventory_provider,
      .boot_control_adapter = &boot_control_adapter,
      .rollback_token_factory = &token_factory,
  });

  const auto preparation = coordinator.build_slot_plan(
      std::string_view("rootfs"),
      std::vector<InstallEvidence>{make_slot_install_evidence()},
      SwitchPolicySnapshot{
          .switch_policy = std::string("confirm_after_boot"),
          .confirm_deadline = std::string("2026-04-08T13:00:00Z"),
      });

  assert_true(preparation.prepared && preparation.references_only_contract_error_types(),
              "SlotSwitchCoordinator should prepare a valid inactive-slot plan and rollback token before any next-boot mutation runs");
  assert_equal(std::string("rootfs_b"), preparation.slot_plan.target_slot,
               "build_slot_plan should choose the inactive slot as the next boot target");
  assert_equal(static_cast<std::size_t>(1), token_factory.calls,
               "build_slot_plan should generate exactly one rollback token before switching boot target");
  assert_equal(static_cast<std::size_t>(0), boot_control_adapter.mutation_calls(),
               "building a slot plan should not mutate boot control state before the rollback token exists");

  const auto switch_result = coordinator.set_next_boot(preparation.slot_plan);
  assert_true(switch_result.switched && switch_result.references_only_contract_error_types(),
              "set_next_boot should switch only after a valid inactive-slot plan is prepared");
  assert_equal(std::string("rootfs_b"), boot_control_adapter.next_target(),
               "set_next_boot should forward the inactive slot target to the boot control adapter");
}

void test_slot_switch_coordinator_rejects_slot_groups_without_inactive_target() {
  using dasall::tests::support::assert_true;

  FakeSlotInventoryProvider inventory_provider;
  inventory_provider.inventory = SlotInventory{
      .slot_group = std::string("rootfs"),
      .active_slot = std::string("rootfs_a"),
      .candidate_slots = {std::string("rootfs_a")},
  };
  FakeRollbackTokenFactory token_factory;
  FakeBootControlAdapter boot_control_adapter("rootfs_a");

  SlotSwitchCoordinator coordinator(SlotSwitchCoordinator::Dependencies{
      .slot_inventory_provider = &inventory_provider,
      .boot_control_adapter = &boot_control_adapter,
      .rollback_token_factory = &token_factory,
  });

  const auto selection = coordinator.select_inactive_slot(std::string_view("rootfs"));
  assert_true(!selection.selected && selection.references_only_contract_error_types(),
              "SlotSwitchCoordinator should reject slot groups that do not expose a distinct inactive target");
}

void test_slot_switch_coordinator_rejects_switch_when_target_is_no_longer_inactive() {
  using dasall::tests::support::assert_true;

  FakeBootControlAdapter boot_control_adapter("rootfs_b");
  SlotSwitchCoordinator coordinator(SlotSwitchCoordinator::Dependencies{
      .slot_inventory_provider = nullptr,
      .boot_control_adapter = &boot_control_adapter,
      .rollback_token_factory = nullptr,
  });

  const auto switch_result = coordinator.set_next_boot(dasall::infra::ota::SlotPlan{
      .active_slot = std::string("rootfs_a"),
      .target_slot = std::string("rootfs_b"),
      .slot_group = std::string("rootfs"),
      .switch_policy = std::string("confirm_after_boot"),
      .confirm_deadline = std::string("2026-04-08T13:00:00Z"),
  });

  assert_true(!switch_result.switched && switch_result.references_only_contract_error_types(),
              "SlotSwitchCoordinator should refuse to switch when the target slot is no longer inactive at mutation time");
}

}  // namespace

int main() {
  try {
    test_slot_switch_coordinator_builds_inactive_slot_plan_and_token_before_switch();
    test_slot_switch_coordinator_rejects_slot_groups_without_inactive_target();
    test_slot_switch_coordinator_rejects_switch_when_target_is_no_longer_inactive();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
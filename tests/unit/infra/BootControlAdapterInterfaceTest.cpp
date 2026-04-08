#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ota/IBootControlAdapter.h"
#include "support/TestAssertions.h"

namespace {

class MockBootControlAdapter final : public dasall::infra::ota::IBootControlAdapter {
 public:
  explicit MockBootControlAdapter(std::string active_target)
      : active_target_(std::move(active_target)) {}

  [[nodiscard]] dasall::infra::ota::BootTargetQueryResult get_active_target() const override {
    if (active_target_.empty()) {
      return dasall::infra::ota::BootTargetQueryResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "active boot target must stay queryable through the adapter boundary",
          "ota.get_active_target",
          "MockBootControlAdapter");
    }

    return dasall::infra::ota::BootTargetQueryResult::success(active_target_);
  }

  [[nodiscard]] dasall::infra::ota::BootMutationResult set_next_boot(
      std::string_view target) override {
    if (target.empty()) {
      return failure_result("ota.set_next_boot");
    }

    next_boot_target_ = std::string(target);
    return success_result(target, "set_next_boot");
  }

  [[nodiscard]] dasall::infra::ota::BootMutationResult mark_boot_success(
      std::string_view target) override {
    if (target.empty()) {
      return failure_result("ota.mark_boot_success");
    }

    successful_targets_.emplace_back(target);
    active_target_ = std::string(target);
    return success_result(target, "mark_boot_success");
  }

  [[nodiscard]] dasall::infra::ota::BootMutationResult mark_boot_failed(
      std::string_view target) override {
    if (target.empty()) {
      return failure_result("ota.mark_boot_failed");
    }

    failed_targets_.emplace_back(target);
    return success_result(target, "mark_boot_failed");
  }

  [[nodiscard]] const std::string& next_boot_target() const { return next_boot_target_; }
  [[nodiscard]] const std::vector<std::string>& successful_targets() const {
    return successful_targets_;
  }
  [[nodiscard]] const std::vector<std::string>& failed_targets() const {
    return failed_targets_;
  }

 private:
  [[nodiscard]] static dasall::infra::ota::BootMutationResult success_result(
      std::string_view target,
      std::string operation) {
    return dasall::infra::ota::BootMutationResult::success(std::string(target),
                                                           std::move(operation));
  }

  [[nodiscard]] static dasall::infra::ota::BootMutationResult failure_result(
      std::string stage) {
    return dasall::infra::ota::BootMutationResult::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        "boot target must remain explicit at the adapter boundary",
        std::move(stage),
        "MockBootControlAdapter");
  }

  std::string active_target_;
  std::string next_boot_target_;
  std::vector<std::string> successful_targets_;
  std::vector<std::string> failed_targets_;
};

bool drive_boot_confirmation_flow(dasall::infra::ota::IBootControlAdapter& adapter,
                                  std::string_view next_target,
                                  bool boot_ok) {
  const auto current = adapter.get_active_target();
  if (!current.resolved || !current.references_only_contract_error_types()) {
    return false;
  }

  const auto boot_switch = adapter.set_next_boot(next_target);
  if (!boot_switch.applied || !boot_switch.references_only_contract_error_types()) {
    return false;
  }

  const auto completion = boot_ok ? adapter.mark_boot_success(next_target)
                                  : adapter.mark_boot_failed(next_target);
  return completion.applied && completion.references_only_contract_error_types();
}

void test_boot_control_adapter_interface_exposes_four_frozen_actions() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MockBootControlAdapter adapter("rootfs_a");

  const auto current = adapter.get_active_target();
  assert_true(current.resolved && current.references_only_contract_error_types(),
              "IBootControlAdapter should expose a queryable active boot target without bootloader-private types");
  assert_equal(std::string("rootfs_a"), current.active_target,
               "get_active_target should preserve the boot target identifier");

  const auto switch_result = adapter.set_next_boot("rootfs_b");
  assert_true(switch_result.applied && switch_result.references_only_contract_error_types(),
              "IBootControlAdapter should expose set_next_boot as a contract-shaped mutation result");
  assert_equal(std::string("rootfs_b"), adapter.next_boot_target(),
               "set_next_boot should preserve the chosen inactive boot target");

  const auto success_result = adapter.mark_boot_success("rootfs_b");
  assert_true(success_result.applied && success_result.references_only_contract_error_types(),
              "IBootControlAdapter should expose mark_boot_success without leaking platform headers");

  const auto failed_result = adapter.mark_boot_failed("rootfs_a");
  assert_true(failed_result.applied && failed_result.references_only_contract_error_types(),
              "IBootControlAdapter should expose mark_boot_failed for confirm-time rollback paths");
}

void test_boot_control_adapter_interface_is_mock_replaceable_for_consumer_flow() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  MockBootControlAdapter success_adapter("rootfs_a");
  const auto success = drive_boot_confirmation_flow(success_adapter, "rootfs_b", true);
  assert_true(success,
              "IBootControlAdapter should be replaceable by a mock adapter in boot confirmation flows");
  assert_equal(static_cast<std::size_t>(1), success_adapter.successful_targets().size(),
               "mock adapter should record mark_boot_success via the frozen interface only");

  MockBootControlAdapter failure_adapter("rootfs_a");
  const auto failure = drive_boot_confirmation_flow(failure_adapter, "rootfs_b", false);
  assert_true(failure,
              "IBootControlAdapter should let tests swap in a failing-boot mock without real platform dependencies");
  assert_equal(static_cast<std::size_t>(1), failure_adapter.failed_targets().size(),
               "mock adapter should record mark_boot_failed via the same interface boundary");
}

void test_boot_control_adapter_interface_rejects_empty_targets() {
  using dasall::tests::support::assert_true;

  MockBootControlAdapter adapter("rootfs_a");

  const auto next_boot_failure = adapter.set_next_boot(std::string_view{});
  assert_true(!next_boot_failure.applied && next_boot_failure.references_only_contract_error_types(),
              "IBootControlAdapter should reject empty next boot targets observably");

  const auto success_failure = adapter.mark_boot_success(std::string_view{});
  assert_true(!success_failure.applied && success_failure.references_only_contract_error_types(),
              "IBootControlAdapter should reject empty boot-success targets observably");

  const auto failed_failure = adapter.mark_boot_failed(std::string_view{});
  assert_true(!failed_failure.applied && failed_failure.references_only_contract_error_types(),
              "IBootControlAdapter should reject empty boot-failed targets observably");
}

}  // namespace

int main() {
  try {
    test_boot_control_adapter_interface_exposes_four_frozen_actions();
    test_boot_control_adapter_interface_is_mock_replaceable_for_consumer_flow();
    test_boot_control_adapter_interface_rejects_empty_targets();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
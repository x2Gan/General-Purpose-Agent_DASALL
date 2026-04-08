#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "ota/IBootControlAdapter.h"
#include "support/TestAssertions.h"

namespace {

void test_boot_control_adapter_interface_keeps_target_and_error_boundaries_stable() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::BootMutationResult;
  using dasall::infra::ota::BootTargetQueryResult;
  using dasall::infra::ota::IBootControlAdapter;

  static_assert(std::is_same_v<decltype(std::declval<const IBootControlAdapter&>().get_active_target()),
                               BootTargetQueryResult>);
  static_assert(std::is_same_v<decltype(std::declval<IBootControlAdapter&>().set_next_boot(
                                   std::declval<std::string_view>())),
                               BootMutationResult>);
  static_assert(std::is_same_v<decltype(std::declval<IBootControlAdapter&>().mark_boot_success(
                                   std::declval<std::string_view>())),
                               BootMutationResult>);
  static_assert(std::is_same_v<decltype(std::declval<IBootControlAdapter&>().mark_boot_failed(
                                   std::declval<std::string_view>())),
                               BootMutationResult>);
  static_assert(std::is_same_v<decltype(BootTargetQueryResult{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(BootMutationResult{}.result_code), ResultCode>);
}

void test_boot_control_adapter_boundary_results_remain_contract_shaped() {
  using dasall::contracts::ResultCode;
  using dasall::tests::support::assert_true;

  const auto query_success = dasall::infra::ota::BootTargetQueryResult::success(std::string("rootfs_a"));
  assert_true(query_success.resolved && query_success.references_only_contract_error_types(),
              "get_active_target should expose only the boot target identifier and contract-shaped failures");

  const auto mutation_success = dasall::infra::ota::BootMutationResult::success(
      std::string("rootfs_b"),
      std::string("set_next_boot"));
  assert_true(mutation_success.applied && mutation_success.references_only_contract_error_types(),
              "boot mutations should remain target-and-operation pairs without platform-private types");

  const auto mutation_failure = dasall::infra::ota::BootMutationResult::failure(
      ResultCode::ValidationFieldMissing,
      std::string("empty boot target"),
      std::string("ota.set_next_boot"),
      std::string("BoundaryBootControl"));
  assert_true(mutation_failure.references_only_contract_error_types(),
              "boot mutation failures should remain observable through contracts ResultCode/ErrorInfo only");
}

}  // namespace

int main() {
  try {
    test_boot_control_adapter_interface_keeps_target_and_error_boundaries_stable();
    test_boot_control_adapter_boundary_results_remain_contract_shaped();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
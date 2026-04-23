#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "AccessTypes.h"
#include "IAccessRuntimeBridge.h"
#include "support/TestAssertions.h"

namespace {

void iaccess_runtime_bridge_dispatch_method_exists() {
  constexpr bool dispatch_method_exists =
      std::is_invocable_r_v<
          dasall::access::RuntimeDispatchResult,
          decltype(&dasall::access::IAccessRuntimeBridge::dispatch),
          dasall::access::IAccessRuntimeBridge*,
          const dasall::access::RuntimeDispatchRequest&>;

  dasall::tests::support::assert_true(
      dispatch_method_exists,
      "IAccessRuntimeBridge::dispatch should be defined with RuntimeDispatchRequest input");
}

void iaccess_runtime_bridge_cancel_method_exists() {
  constexpr bool cancel_method_exists =
      std::is_invocable_r_v<
          bool,
          decltype(&dasall::access::IAccessRuntimeBridge::cancel),
          dasall::access::IAccessRuntimeBridge*,
          std::string_view,
          std::string_view>;

  dasall::tests::support::assert_true(
      cancel_method_exists,
      "IAccessRuntimeBridge::cancel should be defined with request_id and actor_ref");
}

void iaccess_runtime_bridge_is_abstract() {
  static_assert(std::is_abstract_v<dasall::access::IAccessRuntimeBridge>);
  dasall::tests::support::assert_true(
      std::is_abstract_v<dasall::access::IAccessRuntimeBridge>,
      "IAccessRuntimeBridge should remain abstract");
}

}  // namespace

int main() {
  try {
    iaccess_runtime_bridge_dispatch_method_exists();
    iaccess_runtime_bridge_cancel_method_exists();
    iaccess_runtime_bridge_is_abstract();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}

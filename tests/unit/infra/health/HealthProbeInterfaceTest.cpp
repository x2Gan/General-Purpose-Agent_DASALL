#include <exception>
#include <iostream>
#include <type_traits>

#include "health/IHealthProbe.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasEvaluateNowMethod = requires {
  &T::evaluate_now;
};

template <typename T>
concept HasRegisterProbeMethod = requires {
  &T::register_probe;
};

void test_health_probe_interface_keeps_probe_result_as_the_only_operation() {
  using dasall::infra::IHealthProbe;
  using dasall::infra::ProbeResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IHealthProbe::probe), ProbeResult (IHealthProbe::*)()>);
  static_assert(std::is_abstract_v<IHealthProbe>);

  assert_true(std::has_virtual_destructor_v<IHealthProbe>,
              "IHealthProbe should remain a pure virtual boundary with a virtual destructor");
}

void test_health_probe_interface_does_not_absorb_monitor_responsibilities() {
  using dasall::infra::IHealthProbe;
  using dasall::tests::support::assert_true;

  static_assert(!HasEvaluateNowMethod<IHealthProbe>);
  static_assert(!HasRegisterProbeMethod<IHealthProbe>);

  assert_true(!std::is_default_constructible_v<IHealthProbe>,
              "IHealthProbe should stay abstract and should not be directly constructible");
}

}  // namespace

int main() {
  try {
    test_health_probe_interface_keeps_probe_result_as_the_only_operation();
    test_health_probe_interface_does_not_absorb_monitor_responsibilities();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
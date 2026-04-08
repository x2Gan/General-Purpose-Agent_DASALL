#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "metrics/IMetricsHealthProbe.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
concept HasProbeMethod = requires {
  &T::probe;
};

class StaticMetricsHealthProbe final : public dasall::infra::metrics::IMetricsHealthProbe {
 public:
  [[nodiscard]] dasall::infra::metrics::MetricsModuleSnapshot snapshot() const override {
    return dasall::infra::metrics::MetricsModuleSnapshot{
        .queue_depth = 3,
        .guard_reject_total = 1,
        .exporter_state = std::string("noop"),
        .degraded = true,
    };
  }
};

void test_metrics_health_probe_interface_keeps_snapshot_as_the_only_operation() {
  using dasall::infra::metrics::IMetricsHealthProbe;
  using dasall::infra::metrics::MetricsModuleSnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&IMetricsHealthProbe::snapshot),
                               MetricsModuleSnapshot (IMetricsHealthProbe::*)() const>);
  static_assert(std::is_abstract_v<IMetricsHealthProbe>);

  StaticMetricsHealthProbe probe;
  const auto snapshot = probe.snapshot();

  assert_true(std::has_virtual_destructor_v<IMetricsHealthProbe>,
              "IMetricsHealthProbe should remain a pure virtual boundary with a virtual destructor");
  assert_true(snapshot.is_valid() && !snapshot.is_healthy() && snapshot.queue_depth == 3 &&
                  snapshot.guard_reject_total == 1 && snapshot.exporter_state == "noop",
              "IMetricsHealthProbe should return a structured metrics-private snapshot that carries queue, guard and exporter state");
}

void test_metrics_health_probe_interface_does_not_absorb_generic_health_probe_semantics() {
  using dasall::infra::metrics::IMetricsHealthProbe;
  using dasall::tests::support::assert_true;

  static_assert(!HasProbeMethod<IMetricsHealthProbe>);

  assert_true(!std::is_default_constructible_v<IMetricsHealthProbe>,
              "IMetricsHealthProbe should stay abstract and should not collapse into the generic infra health probe contract");
}

}  // namespace

int main() {
  try {
    test_metrics_health_probe_interface_keeps_snapshot_as_the_only_operation();
    test_metrics_health_probe_interface_does_not_absorb_generic_health_probe_semantics();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <type_traits>

#include "secret/ISecretHealthSource.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasProbeMethod = requires {
  &T::probe;
};

template <typename T>
concept HasEvaluateNowMethod = requires {
  &T::evaluate_now;
};

template <typename T>
concept HasGetSnapshotMethod = requires {
  &T::get_snapshot;
};

class StaticSecretHealthSource final : public dasall::infra::secret::ISecretHealthSource {
 public:
  [[nodiscard]] dasall::infra::secret::SecretHealthSnapshot sample_secret_health() const override {
    return dasall::infra::secret::SecretHealthSnapshot{
        .backend_available = true,
        .cache_stale = false,
        .active_lease_count = 2,
        .rotation_backlog = 0,
        .degraded = false,
    };
  }
};

void test_secret_health_source_interface_keeps_snapshot_as_the_only_operation() {
  using dasall::infra::secret::ISecretHealthSource;
  using dasall::infra::secret::SecretHealthSnapshot;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&ISecretHealthSource::sample_secret_health),
                               SecretHealthSnapshot (ISecretHealthSource::*)() const>);
  static_assert(std::is_abstract_v<ISecretHealthSource>);

  StaticSecretHealthSource health_source;
  const auto snapshot = health_source.sample_secret_health();

  assert_true(std::has_virtual_destructor_v<ISecretHealthSource>,
              "ISecretHealthSource should remain a pure abstract secret-private health boundary with a virtual destructor");
  assert_true(snapshot.is_healthy() && snapshot.active_lease_count == 2 &&
                  !snapshot.has_rotation_backlog(),
              "ISecretHealthSource should return a secret-private snapshot with backend, cache and lease state");
}

void test_secret_health_source_interface_does_not_absorb_generic_health_probe_contracts() {
  using dasall::infra::secret::ISecretHealthSource;
  using dasall::tests::support::assert_true;

  static_assert(!HasProbeMethod<ISecretHealthSource>);
  static_assert(!HasEvaluateNowMethod<ISecretHealthSource>);
  static_assert(!HasGetSnapshotMethod<ISecretHealthSource>);

  assert_true(!std::is_default_constructible_v<ISecretHealthSource>,
              "ISecretHealthSource should stay abstract and should not collapse into generic infra health probe or monitor contracts");
}

}  // namespace

int main() {
  try {
    test_secret_health_source_interface_keeps_snapshot_as_the_only_operation();
    test_secret_health_source_interface_does_not_absorb_generic_health_probe_contracts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <cstdint>
#include <exception>
#include <iostream>
#include <string_view>
#include <type_traits>

#include "health/IHealthPolicy.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
concept HasRegisterProbeMethod = requires {
  &T::register_probe;
};

template <typename T>
concept HasSubscribeMethod = requires {
  &T::subscribe;
};

class NullHealthPolicy final : public dasall::infra::IHealthPolicy {
 public:
  [[nodiscard]] dasall::infra::HealthPolicyEvaluationResult evaluate(
      dasall::infra::ProbeResultView results) const override {
    if (!results.is_valid()) {
      return dasall::infra::HealthPolicyEvaluationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "probe result view must use a non-null data pointer when size is non-zero",
          "health.policy.evaluate",
          "NullHealthPolicy");
    }

    if (results.size >= 3) {
      return dasall::infra::HealthPolicyEvaluationResult::success(dasall::infra::HealthSnapshot{
          .liveness = true,
          .readiness = true,
          .degraded = false,
          .failed_components = {},
      });
    }

    return dasall::infra::HealthPolicyEvaluationResult::success(dasall::infra::HealthSnapshot{
        .liveness = true,
        .readiness = false,
        .degraded = true,
        .failed_components = {"probe-threshold-placeholder"},
    });
  }

  [[nodiscard]] std::string_view policy_version() const override {
    return "v1-placeholder";
  }
};

[[nodiscard]] const dasall::infra::ProbeResult* make_placeholder_results() {
  return reinterpret_cast<const dasall::infra::ProbeResult*>(static_cast<std::uintptr_t>(0x1));
}

void test_health_policy_interface_keeps_threshold_inputs_and_snapshot_outputs_constrained() {
  using dasall::infra::HealthPolicyEvaluationResult;
  using dasall::infra::HealthSnapshot;
  using dasall::infra::IHealthPolicy;
  using dasall::infra::ProbeResultView;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(HealthPolicyEvaluationResult{}.snapshot), HealthSnapshot>);
  static_assert(std::is_same_v<decltype(ProbeResultView{}.size), std::size_t>);
  static_assert(!HasRegisterProbeMethod<IHealthPolicy>);
  static_assert(!HasSubscribeMethod<IHealthPolicy>);

  NullHealthPolicy policy;

  const auto healthy_result = policy.evaluate(ProbeResultView{
      .data = make_placeholder_results(),
      .size = 3,
  });
  assert_true(healthy_result.ok,
              "IHealthPolicy skeleton should accept a non-empty result view and produce a snapshot");
  assert_true(healthy_result.snapshot.is_ready(),
              "policy threshold placeholder should still be able to report a ready snapshot");
  assert_true(policy.policy_version() == std::string_view("v1-placeholder"),
              "policy version should stay as a string placeholder until version encoding is frozen");

  const auto degraded_result = policy.evaluate(ProbeResultView{});
  assert_true(degraded_result.ok,
              "IHealthPolicy skeleton should admit an empty result view for degraded placeholder evaluation");
  assert_true(degraded_result.snapshot.is_degraded_state(),
              "empty result view should still map to a constrained degraded snapshot placeholder");
}

void test_health_policy_interface_rejects_invalid_result_views_observably() {
  using dasall::infra::ProbeResultView;
  using dasall::tests::support::assert_true;

  NullHealthPolicy policy;

  const auto invalid_result = policy.evaluate(ProbeResultView{
      .data = nullptr,
      .size = 2,
  });

  assert_true(!invalid_result.ok,
              "IHealthPolicy skeleton should reject a non-zero result count without a data pointer");
  assert_true(invalid_result.references_only_contract_error_types(),
              "IHealthPolicy validation failures should stay within contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_health_policy_interface_keeps_threshold_inputs_and_snapshot_outputs_constrained();
    test_health_policy_interface_rejects_invalid_result_views_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
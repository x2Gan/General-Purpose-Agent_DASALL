#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "watchdog/ITimeoutPolicy.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

template <typename T>
[[nodiscard]] std::shared_ptr<const T> make_placeholder_ptr() {
  return std::shared_ptr<const T>(
      reinterpret_cast<const T*>(static_cast<std::uintptr_t>(0x1)),
      [](const T*) {});
}

class NullTimeoutPolicy final : public dasall::infra::watchdog::ITimeoutPolicy {
 public:
  [[nodiscard]] dasall::infra::watchdog::TimeoutPolicyEvaluationResult evaluate(
      std::shared_ptr<const dasall::infra::watchdog::HeartbeatSample> candidate,
      const dasall::infra::watchdog::TimeoutHistoryWindow& history) const override {
    if (candidate == nullptr || !history.has_bindable_inputs()) {
      return dasall::infra::watchdog::TimeoutPolicyEvaluationResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "timeout policy requires a bindable candidate and a non-empty history window",
          "watchdog.evaluate_timeout_policy",
          "NullTimeoutPolicy");
    }

    return dasall::infra::watchdog::TimeoutPolicyEvaluationResult::success(
        make_placeholder_ptr<dasall::infra::watchdog::TimeoutDecision>());
  }
};

void test_timeout_policy_interface_binds_candidate_history_and_decision_types() {
  using dasall::infra::watchdog::HeartbeatSample;
  using dasall::infra::watchdog::ITimeoutPolicy;
  using dasall::infra::watchdog::TimeoutDecision;
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::infra::watchdog::TimeoutPolicyEvaluationResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&ITimeoutPolicy::evaluate),
                               TimeoutPolicyEvaluationResult (ITimeoutPolicy::*)(
                                   std::shared_ptr<const HeartbeatSample>,
                                   const TimeoutHistoryWindow&) const>);
  static_assert(std::is_same_v<decltype(TimeoutHistoryWindow{}.recent_samples),
                               std::vector<std::shared_ptr<const HeartbeatSample>>>);
  static_assert(std::is_same_v<decltype(TimeoutHistoryWindow{}.prior_decisions),
                               std::vector<std::shared_ptr<const TimeoutDecision>>>);
  static_assert(std::is_same_v<decltype(TimeoutPolicyEvaluationResult{}.decision),
                               std::shared_ptr<const TimeoutDecision>>);

  NullTimeoutPolicy policy;
  TimeoutHistoryWindow history;
  history.recent_samples.push_back(make_placeholder_ptr<HeartbeatSample>());

  const auto result = policy.evaluate(make_placeholder_ptr<HeartbeatSample>(), history);
  assert_true(result.ok && result.has_decision(),
              "ITimeoutPolicy should bind candidate/history inputs and a private TimeoutDecision output without coupling to execution layers");
}

void test_timeout_policy_interface_rejects_unbound_inputs_observably() {
  using dasall::infra::watchdog::TimeoutHistoryWindow;
  using dasall::tests::support::assert_true;

  NullTimeoutPolicy policy;
  const auto result = policy.evaluate(nullptr, TimeoutHistoryWindow{});

  assert_true(!result.ok,
              "ITimeoutPolicy should reject missing candidate/history bindings before policy engine details are implemented");
  assert_true(result.references_only_contract_error_types(),
              "timeout policy binding failures should remain expressible through contracts ResultCode/ErrorInfo");
}

}  // namespace

int main() {
  try {
    test_timeout_policy_interface_binds_candidate_history_and_decision_types();
    test_timeout_policy_interface_rejects_unbound_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
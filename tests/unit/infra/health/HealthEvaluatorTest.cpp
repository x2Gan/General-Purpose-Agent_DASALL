#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "health/HealthEvaluator.h"
#include "health/ProbeTypes.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::ProbeResult make_result(std::string probe_name,
                                                     dasall::infra::ProbeStatus status,
                                                     std::optional<dasall::contracts::ResultCode> error_code = std::nullopt) {
  return dasall::infra::ProbeResult{
      .probe_name = std::move(probe_name),
      .status = status,
      .latency_ms = 3,
      .error_code = error_code,
      .detail_ref = error_code.has_value() ? std::string("health://probe/failure") : std::string(),
      .timestamp = 1712361600000,
  };
}

void test_health_evaluator_classifies_healthy_degraded_and_unhealthy_snapshots() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthEvaluator;
  using dasall::infra::ProbeResultView;
  using dasall::infra::ProbeStatus;
  using dasall::tests::support::assert_true;

  HealthEvaluator evaluator;

  const auto invalid = evaluator.evaluate(ProbeResultView{.data = nullptr, .size = 1});
  assert_true(!invalid.ok && invalid.references_only_contract_error_types() &&
                  invalid.result_code.has_value() &&
                  *invalid.result_code == ResultCode::PolicyDenied,
              "HealthEvaluator should reject invalid or empty ProbeResultView inputs explicitly");

  const std::vector<dasall::infra::ProbeResult> healthy_results = {
      make_result("config_center", ProbeStatus::Healthy),
      make_result("logging_sink", ProbeStatus::Healthy),
  };
  const std::vector<dasall::infra::ProbeResult> degraded_results = {
      make_result("config_center", ProbeStatus::Healthy),
      make_result("logging_sink", ProbeStatus::Degraded, ResultCode::ProviderTimeout),
  };
  const std::vector<dasall::infra::ProbeResult> unhealthy_results = {
      make_result("config_center", ProbeStatus::Healthy),
      make_result("logging_sink", ProbeStatus::Unhealthy, ResultCode::ToolExecutionFailed),
  };

  const auto healthy_snapshot = evaluator.evaluate(
      ProbeResultView{.data = healthy_results.data(), .size = healthy_results.size()});
  const auto degraded_snapshot = evaluator.evaluate(
      ProbeResultView{.data = degraded_results.data(), .size = degraded_results.size()});
  const auto unhealthy_snapshot = evaluator.evaluate(
      ProbeResultView{.data = unhealthy_results.data(), .size = unhealthy_results.size()});

  assert_true(healthy_snapshot.ok && healthy_snapshot.snapshot.is_ready() &&
                  !healthy_snapshot.snapshot.degraded &&
                  evaluator.policy_version() == std::string_view("health-evaluator/default-v1"),
              "HealthEvaluator should classify all-healthy probe sets as ready and expose a stable policy version");
  assert_true(degraded_snapshot.ok && degraded_snapshot.snapshot.is_degraded_state() &&
                  degraded_snapshot.snapshot.failed_components.size() == 1U &&
                  degraded_snapshot.snapshot.failed_components.front() == "logging_sink",
              "HealthEvaluator should classify degraded probe sets into the degraded snapshot state and preserve failed component names");
  assert_true(unhealthy_snapshot.ok && unhealthy_snapshot.snapshot.is_failed_state() &&
                  !unhealthy_snapshot.snapshot.liveness,
              "HealthEvaluator should classify unhealthy probe sets into the failed snapshot state");
}

void test_health_evaluator_emits_transitions_only_when_state_changes() {
  using dasall::infra::HealthEvaluator;
  using dasall::infra::HealthSnapshot;
  using dasall::tests::support::assert_true;

  HealthEvaluator evaluator;

  const HealthSnapshot healthy{
      .liveness = true,
      .readiness = true,
      .degraded = false,
      .failed_components = {},
      .version = 1,
      .timestamp = 1712361600000,
  };
  const HealthSnapshot degraded{
      .liveness = true,
      .readiness = false,
      .degraded = true,
      .failed_components = {std::string("logging_sink")},
      .version = 2,
      .timestamp = 1712361601000,
  };

  const auto transition = evaluator.evaluate_transition(healthy, degraded);
  const auto same_state = evaluator.evaluate_transition(healthy, healthy);

  assert_true(transition.has_required_fields() && transition.trigger_probe == "logging_sink" &&
                  transition.reason.find("healthy->degraded") != std::string::npos,
              "HealthEvaluator should emit a structured transition when the snapshot state changes");
  assert_true(!same_state.has_required_fields(),
              "HealthEvaluator should avoid emitting a transition when the snapshot state does not change");
}

}  // namespace

int main() {
  try {
    test_health_evaluator_classifies_healthy_degraded_and_unhealthy_snapshots();
    test_health_evaluator_emits_transitions_only_when_state_changes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
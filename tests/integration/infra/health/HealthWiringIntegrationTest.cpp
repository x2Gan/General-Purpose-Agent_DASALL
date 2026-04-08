#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "health/HealthEvaluator.h"
#include "health/ProbeExecutor.h"
#include "health/RecoveryHintEmitter.h"
#include "support/TestAssertions.h"

namespace {

class StaticHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  explicit StaticHealthProbe(dasall::infra::ProbeResult result)
      : result_(std::move(result)) {}

  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    return result_;
  }

 private:
  dasall::infra::ProbeResult result_;
};

class ThrowingHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    throw std::runtime_error("health probe failure");
  }
};

[[nodiscard]] dasall::infra::ProbeResult make_healthy_result(
    std::string probe_name) {
  return dasall::infra::ProbeResult{
      .probe_name = std::move(probe_name),
      .status = dasall::infra::ProbeStatus::Healthy,
      .latency_ms = 1,
      .error_code = std::nullopt,
      .detail_ref = std::string(),
      .timestamp = 1712448000000,
  };
}

void register_probe(dasall::infra::ProbeRegistry& registry,
                    std::string probe_name,
                    std::string probe_group,
                    dasall::infra::IHealthProbe& probe) {
  using dasall::infra::HealthProbeRegistration;
  using dasall::tests::support::assert_true;

  const auto registration = registry.register_probe(HealthProbeRegistration{
      .probe_name = std::move(probe_name),
      .probe_group = std::move(probe_group),
      .probe = &probe,
  });
  assert_true(registration.ok,
              "health wiring integration requires each synthetic probe registration to succeed");
}

[[nodiscard]] std::vector<dasall::infra::ProbeResult> collect_results(
    dasall::infra::ProbeExecutor& executor) {
  auto liveness_results = executor.execute_batch("liveness");
  auto readiness_results = executor.execute_batch("readiness");
  liveness_results.insert(liveness_results.end(),
                          readiness_results.begin(),
                          readiness_results.end());
  return liveness_results;
}

void test_health_wiring_integration_evaluates_registered_probes() {
  using dasall::infra::HealthEvaluator;
  using dasall::infra::ProbeExecutor;
  using dasall::infra::ProbeRegistry;
  using dasall::tests::support::assert_true;

  ProbeRegistry registry;
  StaticHealthProbe runtime_watchdog(make_healthy_result("runtime_watchdog"));
  StaticHealthProbe config_center(make_healthy_result("config_center"));
  register_probe(registry, "runtime_watchdog", "liveness", runtime_watchdog);
  register_probe(registry, "config_center", "readiness", config_center);

  ProbeExecutor executor(registry);
  const auto results = collect_results(executor);

  assert_true(results.size() == 2U && results.front().has_consistent_state() &&
                  results.back().has_consistent_state(),
              "health wiring integration should produce consistent liveness/readiness probe results after registration");

  HealthEvaluator evaluator;
  const auto evaluation = evaluator.evaluate(
      dasall::infra::ProbeResultView{.data = results.data(), .size = results.size()});

  assert_true(evaluation.ok && evaluation.snapshot.is_ready() &&
                  evaluation.snapshot.failed_components.empty(),
              "health wiring integration should classify the all-healthy probe batch as a ready snapshot");
}

void test_health_wiring_integration_emits_recovery_hint_after_repeated_failures() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthEvaluator;
  using dasall::infra::ProbeExecutor;
  using dasall::infra::ProbeRegistry;
  using dasall::infra::RecoveryHintEmitter;
  using dasall::infra::RecoveryHintSeverity;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ProbeRegistry registry;
  ThrowingHealthProbe runtime_watchdog;
  StaticHealthProbe config_center(make_healthy_result("config_center"));
  register_probe(registry, "runtime_watchdog", "liveness", runtime_watchdog);
  register_probe(registry, "config_center", "readiness", config_center);

  ProbeExecutor executor(registry);
  HealthEvaluator evaluator;
  dasall::infra::HealthSnapshot latest_snapshot;

  for (int round = 0; round < 3; ++round) {
    const auto results = collect_results(executor);
    const auto evaluation = evaluator.evaluate(
        dasall::infra::ProbeResultView{.data = results.data(), .size = results.size()});
    assert_true(evaluation.ok,
                "health wiring integration should keep the evaluation stage observable across repeated probe failures");
    latest_snapshot = evaluation.snapshot;
  }

  assert_true(latest_snapshot.is_failed_state() &&
                  latest_snapshot.failed_components.size() == 1U,
              "health wiring integration should escalate repeated execution failures into an unhealthy snapshot");
  assert_equal("runtime_watchdog",
               latest_snapshot.failed_components.front(),
               "health wiring integration should preserve the failed probe name in the unhealthy snapshot");

  RecoveryHintEmitter emitter;
  const auto hint_result = emitter.emit_hint(latest_snapshot, "integration failure path");

  assert_true(hint_result.ok && hint_result.hint.has_required_fields() &&
                  hint_result.hint.reason_code == ResultCode::RuntimeRetryExhausted &&
                  hint_result.hint.severity == RecoveryHintSeverity::Critical &&
                  hint_result.hint.evidence_ref.find("runtime_watchdog") != std::string::npos,
              "health wiring integration should emit a critical advisory recovery hint for repeated probe execution failures");
}

}  // namespace

int main() {
  try {
    test_health_wiring_integration_evaluates_registered_probes();
    test_health_wiring_integration_emits_recovery_hint_after_repeated_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
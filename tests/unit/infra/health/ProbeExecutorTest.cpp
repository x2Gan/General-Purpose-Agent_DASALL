#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "health/ProbeExecutor.h"
#include "support/TestAssertions.h"

namespace {

class SleepingHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  explicit SleepingHealthProbe(std::int64_t sleep_ms) : sleep_ms_(sleep_ms) {}

  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms_));
    return dasall::infra::ProbeResult{
        .probe_name = std::string("sleeping_probe"),
        .status = dasall::infra::ProbeStatus::Healthy,
        .latency_ms = 0,
        .error_code = std::nullopt,
        .detail_ref = std::string(),
        .timestamp = 1712361600000,
    };
  }

 private:
  std::int64_t sleep_ms_ = 0;
};

class ThrowingHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    throw std::runtime_error("probe failure");
  }
};

class HealthyHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    return dasall::infra::ProbeResult{
        .probe_name = std::string("healthy_probe"),
        .status = dasall::infra::ProbeStatus::Healthy,
        .latency_ms = 0,
        .error_code = std::nullopt,
        .detail_ref = std::string(),
        .timestamp = 1712361600000,
    };
  }
};

class FailingHealthProbe final : public dasall::infra::IHealthProbe {
 public:
  [[nodiscard]] dasall::infra::ProbeResult probe() override {
    return dasall::infra::ProbeResult{
        .probe_name = std::string("failing_probe"),
        .status = dasall::infra::ProbeStatus::Degraded,
        .latency_ms = 0,
        .error_code = dasall::contracts::ResultCode::ProviderTimeout,
        .detail_ref = std::string("health://probe/failure"),
        .timestamp = 1712361600000,
    };
  }
};

void test_probe_executor_structures_timeout_and_exception_failures() {
  using dasall::contracts::ResultCode;
  using dasall::infra::HealthProbeRegistration;
  using dasall::infra::ProbeExecutor;
  using dasall::infra::ProbeRegistry;
  using dasall::tests::support::assert_true;

  ProbeRegistry registry;
  SleepingHealthProbe timeout_probe(5);
  ThrowingHealthProbe exception_probe;
  ProbeExecutor executor(registry);

  assert_true(registry.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("timeout_probe"),
                  .probe_group = std::string("liveness"),
                  .probe = &timeout_probe,
              }).ok,
              "ProbeExecutor tests require a registered timeout probe");
  assert_true(registry.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("exception_probe"),
                  .probe_group = std::string("liveness"),
                  .probe = &exception_probe,
              }).ok,
              "ProbeExecutor tests require a registered exception probe");

  auto timeout_descriptor = *registry.find_descriptor("timeout_probe");
  timeout_descriptor.timeout_ms = 1;
  timeout_descriptor.interval_ms = 10;

  const auto timeout_result = executor.execute_once(timeout_descriptor);
  const auto exception_result = executor.execute_once(*registry.find_descriptor("exception_probe"));

  assert_true(timeout_result.status == dasall::infra::ProbeStatus::Degraded &&
                  timeout_result.error_code.has_value() &&
                  *timeout_result.error_code == ResultCode::ProviderTimeout &&
                  !timeout_result.detail_ref.empty() &&
                  executor.consecutive_failure_count("timeout_probe") == 1U,
              "ProbeExecutor should map synchronous timeout overruns to a structured timeout failure result");

  assert_true(exception_result.status == dasall::infra::ProbeStatus::Degraded &&
                  exception_result.error_code.has_value() &&
                  *exception_result.error_code == ResultCode::ToolExecutionFailed &&
                  exception_result.detail_ref.find("exception") != std::string::npos &&
                  executor.consecutive_failure_count("exception_probe") == 1U,
              "ProbeExecutor should catch probe exceptions and convert them into structured execution failures");
}

void test_probe_executor_executes_batches_and_tracks_consecutive_failures() {
  using dasall::infra::HealthProbeRegistration;
  using dasall::infra::ProbeExecutor;
  using dasall::infra::ProbeRegistry;
  using dasall::infra::ProbeStatus;
  using dasall::tests::support::assert_true;

  ProbeRegistry registry;
  FailingHealthProbe failing_probe;
  HealthyHealthProbe healthy_probe;
  ProbeExecutor executor(registry);

  assert_true(registry.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("alpha_failure"),
                  .probe_group = std::string("readiness"),
                  .probe = &failing_probe,
              }).ok,
              "ProbeExecutor batch tests require a registered failing probe");
  assert_true(registry.register_probe(HealthProbeRegistration{
                  .probe_name = std::string("zeta_healthy"),
                  .probe_group = std::string("readiness"),
                  .probe = &healthy_probe,
              }).ok,
              "ProbeExecutor batch tests require a registered healthy probe");

  std::vector<dasall::infra::ProbeResult> third_batch;
  for (int round = 0; round < 3; ++round) {
    third_batch = executor.execute_batch("readiness");
  }

  assert_true(third_batch.size() == 2U,
              "ProbeExecutor should execute every probe descriptor returned for a group batch");
  assert_true(third_batch.front().probe_name == "alpha_failure" &&
                  third_batch.front().status == ProbeStatus::Unhealthy &&
                  third_batch.back().probe_name == "zeta_healthy" &&
                  third_batch.back().status == ProbeStatus::Healthy,
              "ProbeExecutor should keep batch ordering stable by registry name ordering and escalate repeated failures to unhealthy");
  assert_true(executor.consecutive_failure_count("alpha_failure") == 3U &&
                  executor.consecutive_failure_count("zeta_healthy") == 0U,
              "ProbeExecutor should keep consecutive failure counters per probe and reset them for successful executions");
}

}  // namespace

int main() {
  try {
    test_probe_executor_structures_timeout_and_exception_failures();
    test_probe_executor_executes_batches_and_tracks_consecutive_failures();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
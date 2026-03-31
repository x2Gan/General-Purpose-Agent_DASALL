#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "health/ProbeTypes.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_probe_descriptor_accepts_supported_groups_and_schedules() {
  using dasall::infra::ProbeCriticality;
  using dasall::infra::ProbeDescriptor;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ProbeDescriptor{}.probe_name), std::string>);
  static_assert(std::is_same_v<decltype(ProbeDescriptor{}.criticality), ProbeCriticality>);

  const ProbeDescriptor descriptor{
      .probe_name = "config_center",
      .group = "liveness",
      .criticality = ProbeCriticality::Critical,
      .interval_ms = 1000,
      .timeout_ms = 250,
  };

  assert_true(descriptor.has_required_fields(),
              "ProbeDescriptor should accept a non-empty name, supported group, fixed criticality, and a valid schedule");
}

void test_probe_descriptor_rejects_unknown_criticality_and_invalid_schedule() {
  using dasall::infra::ProbeCriticality;
  using dasall::infra::ProbeDescriptor;
  using dasall::tests::support::assert_true;

  const ProbeDescriptor unsupported_group{
      .probe_name = "logging_sink",
      .group = "metrics",
      .criticality = ProbeCriticality::NonCritical,
      .interval_ms = 1000,
      .timeout_ms = 250,
  };

  const ProbeDescriptor invalid_schedule{
      .probe_name = "logging_sink",
      .group = "readiness",
      .criticality = ProbeCriticality::Critical,
      .interval_ms = 200,
      .timeout_ms = 500,
  };

  const ProbeDescriptor unspecified_criticality{
      .probe_name = "logging_sink",
      .group = "readiness",
      .criticality = ProbeCriticality::Unspecified,
      .interval_ms = 1000,
      .timeout_ms = 250,
  };

  assert_true(!unsupported_group.has_required_fields(),
              "ProbeDescriptor should reject unsupported probe groups until additional groups are explicitly frozen");
  assert_true(!invalid_schedule.has_required_fields(),
              "ProbeDescriptor should reject timeout windows that exceed the evaluation interval");
  assert_true(!unspecified_criticality.has_required_fields(),
              "ProbeDescriptor should reject an unspecified criticality placeholder");
}

void test_probe_result_accepts_structured_status_and_error_mapping() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ProbeResult;
  using dasall::infra::ProbeStatus;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(ProbeResult{}.status), ProbeStatus>);
  static_assert(std::is_same_v<decltype(ProbeResult{}.error_code), std::optional<ResultCode>>);

  const ProbeResult healthy{
      .probe_name = "config_center",
      .status = ProbeStatus::Healthy,
      .latency_ms = 12,
      .error_code = std::nullopt,
      .detail_ref = std::string(),
      .timestamp = 1711785600100,
  };

  const ProbeResult degraded{
      .probe_name = "metrics_exporter",
      .status = ProbeStatus::Degraded,
      .latency_ms = 320,
      .error_code = ResultCode::ProviderTimeout,
      .detail_ref = "health://probe/metrics_exporter/timeout",
      .timestamp = 1711785600200,
  };

  assert_true(healthy.has_consistent_state(),
              "ProbeResult should treat a successful healthy probe as a consistent state");
  assert_true(degraded.has_consistent_state(),
              "ProbeResult should admit degraded results when failure details remain observable and error mapping stays in contracts");
}

void test_probe_result_rejects_missing_failure_detail_and_unknown_error_mapping() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ProbeResult;
  using dasall::infra::ProbeStatus;
  using dasall::tests::support::assert_true;

  const ProbeResult missing_detail{
      .probe_name = "health_scheduler",
      .status = ProbeStatus::Degraded,
      .latency_ms = 400,
      .error_code = ResultCode::ProviderTimeout,
      .detail_ref = std::string(),
      .timestamp = 1711785600300,
  };

  const ProbeResult unknown_mapping{
      .probe_name = "health_scheduler",
      .status = ProbeStatus::Unhealthy,
      .latency_ms = 600,
      .error_code = static_cast<ResultCode>(9999),
      .detail_ref = "health://probe/health_scheduler/error",
      .timestamp = 1711785600400,
  };

  const ProbeResult healthy_with_error{
      .probe_name = "health_scheduler",
      .status = ProbeStatus::Healthy,
      .latency_ms = 10,
      .error_code = ResultCode::ProviderTimeout,
      .detail_ref = "health://probe/health_scheduler/error",
      .timestamp = 1711785600500,
  };

  assert_true(!missing_detail.has_consistent_state(),
              "ProbeResult should reject degraded or unhealthy states that hide their detail_ref evidence");
  assert_true(!unknown_mapping.has_consistent_state(),
              "ProbeResult should reject error codes that fall outside the frozen contracts mapping ranges");
  assert_true(!healthy_with_error.has_consistent_state(),
              "ProbeResult should reject healthy states that still carry an error code");
}

}  // namespace

int main() {
  try {
    test_probe_descriptor_accepts_supported_groups_and_schedules();
    test_probe_descriptor_rejects_unknown_criticality_and_invalid_schedule();
    test_probe_result_accepts_structured_status_and_error_mapping();
    test_probe_result_rejects_missing_failure_detail_and_unknown_error_mapping();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
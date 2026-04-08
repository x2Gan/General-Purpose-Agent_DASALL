#include <exception>
#include <iostream>
#include <optional>
#include <type_traits>

#include "../../../infra/include/metrics/IMeter.h"
#include "support/TestAssertions.h"

namespace {

void test_meter_interface_keeps_identity_and_sample_boundary_local() {
  using dasall::infra::metrics::IMeter;
  using dasall::infra::metrics::InstrumentHandle;
  using dasall::infra::metrics::MetricIdentity;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::infra::metrics::MetricSample;

  static_assert(std::is_same_v<decltype(&IMeter::create_counter),
                               std::optional<InstrumentHandle> (IMeter::*)(
                                   const MetricIdentity&)>);
  static_assert(std::is_same_v<decltype(&IMeter::create_gauge),
                               std::optional<InstrumentHandle> (IMeter::*)(
                                   const MetricIdentity&)>);
  static_assert(std::is_same_v<decltype(&IMeter::create_histogram),
                               std::optional<InstrumentHandle> (IMeter::*)(
                                   const MetricIdentity&)>);
  static_assert(std::is_same_v<decltype(std::declval<IMeter&>().record(
                                   std::declval<const MetricSample&>())),
                               MetricsOperationStatus>);
}

void test_meter_interface_keeps_handle_and_error_surface_binary() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::InstrumentHandle;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.error), std::optional<ErrorInfo>>);

  const auto failure = MetricsOperationStatus::failure(ResultCode::ValidationFieldMissing,
                                                       "metric sample placeholder must stay explicit",
                                                       "metrics.record",
                                                       "IMeter");

  assert_true(InstrumentHandle{.instrument_key = std::string("counter://metrics.samples")}.is_valid(),
              "instrument handles should remain binary-valid when they carry a frozen placeholder key");
  assert_true(!InstrumentHandle{}.is_valid(),
              "instrument handles should reject an empty placeholder key");
  assert_true(!failure.ok,
              "meter boundary failures should remain explicit failures");
  assert_true(failure.references_only_contract_error_types(),
              "IMeter should expose only contracts ResultCode/ErrorInfo across the boundary");
}

}  // namespace

int main() {
  try {
    test_meter_interface_keeps_identity_and_sample_boundary_local();
    test_meter_interface_keeps_handle_and_error_surface_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <iostream>
#include <optional>
#include <type_traits>
#include <utility>

#include "metrics/IMeter.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

class NullMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity&) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = std::string("counter://metrics.provider")};
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity&) override {
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = std::string("gauge://metrics.provider")};
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity&) override {
    return std::nullopt;
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample&) override {
    return dasall::infra::metrics::MetricsOperationStatus::failure(
        dasall::contracts::ResultCode::ValidationFieldMissing,
        "metric sample placeholder is not frozen until MetricTypes lands",
        "metrics.record",
        "NullMeter");
  }
};

void test_meter_interface_keeps_forward_declared_identity_and_sample_signatures() {
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
  static_assert(std::is_same_v<decltype(std::declval<NullMeter&>().record(
                                   std::declval<const MetricSample&>())),
                               MetricsOperationStatus>);
}

void test_meter_interface_keeps_instrument_handle_guards_binary() {
  using dasall::infra::metrics::InstrumentHandle;
  using dasall::tests::support::assert_true;

  const InstrumentHandle valid_handle{.instrument_key = std::string("counter://metrics.samples")};
  const InstrumentHandle invalid_handle{};

  assert_true(valid_handle.is_valid(),
              "instrument handles should remain valid when the frozen instrument key is present");
  assert_true(!invalid_handle.is_valid(),
              "instrument handles should reject empty placeholder keys");
}

void test_meter_interface_error_surface_stays_inside_contract_error_types() {
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_true;

  const auto result = MetricsOperationStatus::failure(
      dasall::contracts::ResultCode::ValidationFieldMissing,
      "metric sample placeholder is not frozen until MetricTypes lands",
      "metrics.record",
      "IMeter");
  assert_true(!result.ok,
              "IMeter placeholder may reject record calls until MetricSample is frozen");
  assert_true(result.references_only_contract_error_types(),
              "record failures should stay inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_meter_interface_keeps_forward_declared_identity_and_sample_signatures();
    test_meter_interface_keeps_instrument_handle_guards_binary();
    test_meter_interface_error_surface_stays_inside_contract_error_types();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
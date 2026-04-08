#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "metrics/IMetricsProvider.h"
#include "support/TestAssertions.h"

namespace {

class NullMetricsProvider final : public dasall::infra::metrics::IMetricsProvider {
 public:
  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig& config) override {
    if (!config.is_valid()) {
      return dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metrics provider config must keep internal/noop defaults explicit and positive intervals",
          "metrics.init",
          "NullMetricsProvider");
    }

    initialized_ = true;
    return dasall::infra::metrics::MetricsOperationStatus::success("metrics-provider://initialized");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    if (!initialized_ || !scope.is_valid()) {
      last_scope_name_.clear();
      return {};
    }

    last_scope_name_ = scope.name;
    return {};
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline& timeout) override {
    if (!initialized_ || !timeout.is_valid()) {
      return dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metrics force_flush timeout must stay explicit after provider init",
          "metrics.force_flush",
          "NullMetricsProvider");
    }

    return dasall::infra::metrics::MetricsOperationStatus::success("metrics-provider://flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline& timeout) override {
    if (!initialized_ || !timeout.is_valid()) {
      return dasall::infra::metrics::MetricsOperationStatus::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "metrics shutdown timeout must stay explicit after provider init",
          "metrics.shutdown",
          "NullMetricsProvider");
    }

    initialized_ = false;
    return dasall::infra::metrics::MetricsOperationStatus::success("metrics-provider://shutdown");
  }

  [[nodiscard]] const std::string& last_scope_name() const {
    return last_scope_name_;
  }

 private:
  bool initialized_ = false;
  std::string last_scope_name_;
};

void test_metrics_provider_interface_accepts_valid_lifecycle_inputs() {
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.state_ref), std::string>);

  NullMetricsProvider provider;
  const auto init_result = provider.init(MetricsProviderConfig{});
  assert_true(init_result.ok,
              "IMetricsProvider skeleton should accept the frozen internal/noop default config placeholder");

  const auto meter = provider.get_meter(MeterScope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  });
  assert_true(!meter,
              "IMetricsProvider compile skeleton may return an empty meter placeholder before IMeter is frozen");
  assert_true(provider.last_scope_name() == "infra.metrics",
              "IMetricsProvider skeleton should still preserve the accepted meter scope name");

  const auto flush_result = provider.force_flush(MetricsCallDeadline{.timeout_ms = 250});
  assert_true(flush_result.ok,
              "IMetricsProvider skeleton should accept an explicit positive flush deadline placeholder");

  const auto shutdown_result = provider.shutdown(MetricsCallDeadline{.timeout_ms = 250});
  assert_true(shutdown_result.ok,
              "IMetricsProvider skeleton should accept an explicit positive shutdown deadline placeholder");
}

void test_metrics_provider_interface_reports_invalid_inputs_observably() {
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::tests::support::assert_true;

  NullMetricsProvider provider;

  const auto invalid_init = provider.init(MetricsProviderConfig{
      .enabled = true,
      .provider_type = std::string(),
      .exporter_type = std::string("noop"),
      .reader_interval_ms = 0,
      .exporter_timeout_ms = 30000,
  });
  assert_true(!invalid_init.ok,
              "IMetricsProvider skeleton should reject empty provider_type or zero reader interval placeholders");
  assert_true(invalid_init.references_only_contract_error_types(),
              "init failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto invalid_meter = provider.get_meter(MeterScope{});
  assert_true(!invalid_meter,
              "IMetricsProvider skeleton should reject missing meter scope names before init succeeds");

  const auto invalid_flush = provider.force_flush(MetricsCallDeadline{});
  assert_true(!invalid_flush.ok,
              "IMetricsProvider skeleton should reject an unset force_flush deadline placeholder");
  assert_true(invalid_flush.references_only_contract_error_types(),
              "force_flush failures should stay inside contracts ResultCode/ErrorInfo types");

  const auto invalid_shutdown = provider.shutdown(MetricsCallDeadline{});
  assert_true(!invalid_shutdown.ok,
              "IMetricsProvider skeleton should reject an unset shutdown deadline placeholder");
  assert_true(invalid_shutdown.references_only_contract_error_types(),
              "shutdown failures should stay inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_metrics_provider_interface_accepts_valid_lifecycle_inputs();
    test_metrics_provider_interface_reports_invalid_inputs_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
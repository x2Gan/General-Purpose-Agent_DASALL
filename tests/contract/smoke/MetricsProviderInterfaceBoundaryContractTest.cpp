#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "../../../infra/include/metrics/IMetricsProvider.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

void test_metrics_provider_interface_keeps_error_surface_inside_contracts() {
  using dasall::contracts::ErrorInfo;
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::IMeter;
  using dasall::infra::metrics::IMetricsProvider;
  using dasall::infra::metrics::MetricsOperationStatus;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.result_code), ResultCode>);
  static_assert(std::is_same_v<decltype(MetricsOperationStatus{}.error), std::optional<ErrorInfo>>);
  static_assert(std::is_same_v<decltype(&IMetricsProvider::get_meter),
                               std::shared_ptr<IMeter> (IMetricsProvider::*)(
                                   const dasall::infra::metrics::MeterScope&)>);

  const auto failure = MetricsOperationStatus::failure(ResultCode::ValidationFieldMissing,
                                                       "provider config must stay explicit",
                                                       "metrics.init",
                                                       "IMetricsProvider");
  assert_true(!failure.ok,
              "metrics provider boundary failures should remain explicit failures");
  assert_true(failure.references_only_contract_error_types(),
              "IMetricsProvider should expose only contracts ResultCode/ErrorInfo types");
}

void test_metrics_provider_interface_keeps_local_placeholder_guards_binary() {
  using dasall::infra::metrics::MeterScope;
  using dasall::infra::metrics::MetricsCallDeadline;
  using dasall::infra::metrics::MetricsProviderConfig;
  using dasall::tests::support::assert_true;

  const MetricsProviderConfig valid_config{};
  const MetricsProviderConfig invalid_config{
      .enabled = true,
      .provider_type = std::string(),
      .exporter_type = std::string("noop"),
      .reader_interval_ms = 0,
      .exporter_timeout_ms = 0,
  };

  const MeterScope valid_scope{
      .name = std::string("infra.metrics"),
      .version = std::string("1.0"),
      .schema_url = std::string("https://opentelemetry.io/schemas/1.26.0"),
  };

  assert_true(valid_config.is_valid(),
              "provider config defaults should stay executable as the frozen placeholder contract");
  assert_true(!invalid_config.is_valid(),
              "provider config should reject empty provider/exporter fields and zero timeouts");
  assert_true(valid_scope.is_valid(),
              "meter scope should stay valid when a non-empty scope name is supplied");
  assert_true(!MeterScope{}.is_valid(),
              "meter scope should reject an empty scope name placeholder");
  assert_true(MetricsCallDeadline{.timeout_ms = 1}.is_valid(),
              "positive timeout values should satisfy the frozen provider deadline guard");
  assert_true(!MetricsCallDeadline{}.is_valid(),
              "zero timeout should remain invalid until lifecycle deadline semantics are expanded");
}

}  // namespace

int main() {
  try {
    test_metrics_provider_interface_keeps_error_surface_inside_contracts();
    test_metrics_provider_interface_keeps_local_placeholder_guards_binary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
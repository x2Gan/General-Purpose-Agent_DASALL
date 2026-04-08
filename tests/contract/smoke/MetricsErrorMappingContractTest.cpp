#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../../infra/include/metrics/MetricsErrors.h"
#include "support/TestAssertions.h"

namespace {

struct MetricsErrorMappingExpectation {
  dasall::infra::metrics::MetricsErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
  std::string_view source_anchor;
};

void test_metrics_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::MetricsErrorMapping;
  using dasall::infra::metrics::map_metrics_error_code;
  using dasall::infra::metrics::metrics_error_code_name;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(MetricsErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<MetricsErrorMappingExpectation, 7> kFrozenMappings{ {
      {MetricsErrorCode::ProviderNotReady,
       "MET_E_PROVIDER_NOT_READY",
       ResultCode::ProviderTimeout,
       "6.6 IMetricsProvider"},
      {MetricsErrorCode::IdentityInvalid,
       "MET_E_IDENTITY_INVALID",
       ResultCode::ValidationFieldMissing,
       "6.5 MetricIdentity"},
      {MetricsErrorCode::LabelCardinalityExceeded,
       "MET_E_LABEL_CARDINALITY_EXCEEDED",
       ResultCode::PolicyDenied,
       "6.8 label exception"},
      {MetricsErrorCode::QueueFull,
       "MET_E_QUEUE_FULL",
       ResultCode::RuntimeRetryExhausted,
       "6.8 queue exception"},
      {MetricsErrorCode::ExportFailure,
       "MET_E_EXPORT_FAILURE",
       ResultCode::ProviderTimeout,
       "6.8 export exception"},
      {MetricsErrorCode::ExportTimeout,
       "MET_E_EXPORT_TIMEOUT",
       ResultCode::ProviderTimeout,
       "6.6 IMetricExporter"},
      {MetricsErrorCode::ConfigInvalid,
       "MET_E_CONFIG_INVALID",
       ResultCode::ValidationFieldMissing,
       "6.9 metrics config"},
  } };

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_metrics_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("metrics error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.name),
                 std::string(metrics_error_code_name(expectation.code)),
                 std::string("metrics error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.source_anchor),
                 std::string(mapping.source_anchor),
                 std::string("metrics error source anchor should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each metrics private error mapping should carry a non-empty reason");
  }
}

void test_metrics_error_names_stay_local_to_metrics_boundary() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::metrics_error_code_name;
  using dasall::tests::support::assert_true;

  constexpr std::array<MetricsErrorCode, 7> kFrozenCodes{
      MetricsErrorCode::ProviderNotReady,
      MetricsErrorCode::IdentityInvalid,
      MetricsErrorCode::LabelCardinalityExceeded,
      MetricsErrorCode::QueueFull,
      MetricsErrorCode::ExportFailure,
      MetricsErrorCode::ExportTimeout,
      MetricsErrorCode::ConfigInvalid,
  };

  for (const auto code : kFrozenCodes) {
    assert_true(metrics_error_code_name(code).starts_with("MET_E_"),
                "metrics private error names should remain inside the MET_E_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_metrics_error_mapping_matrix_stays_frozen();
    test_metrics_error_names_stay_local_to_metrics_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
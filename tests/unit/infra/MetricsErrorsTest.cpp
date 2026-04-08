#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "metrics/MetricsErrors.h"
#include "support/TestAssertions.h"

namespace {

struct MetricsErrorExpectation {
  dasall::infra::metrics::MetricsErrorCode code;
  int raw_value;
  std::string_view name;
};

void test_metrics_error_code_values_and_names_are_stable() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::metrics_error_code_name;
  using dasall::tests::support::assert_equal;

  constexpr std::array<MetricsErrorExpectation, 7> kFrozenNames{ {
      {MetricsErrorCode::ProviderNotReady, 1, "MET_E_PROVIDER_NOT_READY"},
      {MetricsErrorCode::IdentityInvalid, 2, "MET_E_IDENTITY_INVALID"},
      {MetricsErrorCode::LabelCardinalityExceeded, 3, "MET_E_LABEL_CARDINALITY_EXCEEDED"},
      {MetricsErrorCode::QueueFull, 4, "MET_E_QUEUE_FULL"},
      {MetricsErrorCode::ExportFailure, 5, "MET_E_EXPORT_FAILURE"},
      {MetricsErrorCode::ExportTimeout, 6, "MET_E_EXPORT_TIMEOUT"},
      {MetricsErrorCode::ConfigInvalid, 7, "MET_E_CONFIG_INVALID"},
  } };

  for (const auto& expectation : kFrozenNames) {
    assert_equal(expectation.raw_value,
                 static_cast<int>(expectation.code),
                 "metrics error enum numeric values should remain stable");
    assert_equal(std::string(expectation.name),
                 std::string(metrics_error_code_name(expectation.code)),
                 "metrics error code names should remain stable");
  }
}

void test_metrics_error_mapping_keeps_source_anchors_observable() {
  using dasall::infra::metrics::MetricsErrorCode;
  using dasall::infra::metrics::map_metrics_error_code;
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
    const auto mapping = map_metrics_error_code(code);
    assert_true(!mapping.source_anchor.empty(),
                "each metrics error mapping should carry a non-empty design source anchor");
    assert_true(!mapping.reason.empty(),
                "each metrics error mapping should carry a non-empty observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_metrics_error_code_values_and_names_are_stable();
    test_metrics_error_mapping_keeps_source_anchors_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
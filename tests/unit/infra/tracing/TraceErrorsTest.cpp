#include <array>
#include <exception>
#include <iostream>
#include <string>

#include "tracing/TraceErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct TraceErrorExpectation {
  dasall::infra::tracing::TraceErrorCode code;
  int raw_value;
  std::string_view name;
};

void test_trace_error_code_values_and_names_are_stable() {
  using dasall::infra::tracing::trace_error_code_name;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::tests::support::assert_equal;

  constexpr std::array<TraceErrorExpectation, 7> kFrozenNames{ {
      {TraceErrorCode::ProviderNotReady, 1, "TRC_E_PROVIDER_NOT_READY"},
      {TraceErrorCode::InvalidContext, 2, "TRC_E_INVALID_CONTEXT"},
      {TraceErrorCode::QueueFull, 3, "TRC_E_QUEUE_FULL"},
      {TraceErrorCode::ExportTimeout, 4, "TRC_E_EXPORT_TIMEOUT"},
      {TraceErrorCode::ExportFailure, 5, "TRC_E_EXPORT_FAILURE"},
      {TraceErrorCode::ShutdownTimeout, 6, "TRC_E_SHUTDOWN_TIMEOUT"},
      {TraceErrorCode::ConfigInvalid, 7, "TRC_E_CONFIG_INVALID"},
  } };

  for (const auto& expectation : kFrozenNames) {
    assert_equal(expectation.raw_value,
                 static_cast<int>(expectation.code),
                 "trace error enum numeric values should remain stable");
    assert_equal(std::string(expectation.name),
                 std::string(trace_error_code_name(expectation.code)),
                 "trace error code names should remain stable");
  }
}

void test_trace_error_mapping_keeps_source_anchors_observable() {
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::tests::support::assert_true;

  constexpr std::array<TraceErrorCode, 7> kFrozenCodes{
      TraceErrorCode::ProviderNotReady,
      TraceErrorCode::InvalidContext,
      TraceErrorCode::QueueFull,
      TraceErrorCode::ExportTimeout,
      TraceErrorCode::ExportFailure,
      TraceErrorCode::ShutdownTimeout,
      TraceErrorCode::ConfigInvalid,
  };

  for (const auto code : kFrozenCodes) {
    const auto mapping = map_trace_error_code(code);
    assert_true(!mapping.source_anchor.empty(),
                "each tracing error mapping should carry a non-empty design source anchor");
    assert_true(!mapping.reason.empty(),
                "each tracing error mapping should carry a non-empty observable reason");
  }
}

}  // namespace

int main() {
  try {
    test_trace_error_code_values_and_names_are_stable();
    test_trace_error_mapping_keeps_source_anchors_observable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
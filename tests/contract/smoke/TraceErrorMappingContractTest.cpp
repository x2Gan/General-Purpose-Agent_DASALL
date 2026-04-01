#include <array>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "../../../infra/include/tracing/TraceErrors.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

struct TraceErrorMappingExpectation {
  dasall::infra::tracing::TraceErrorCode code;
  std::string_view name;
  dasall::contracts::ResultCode result_code;
  std::string_view source_anchor;
};

void test_trace_error_mapping_matrix_stays_frozen() {
  using dasall::contracts::ResultCode;
  using dasall::infra::tracing::map_trace_error_code;
  using dasall::infra::tracing::trace_error_code_name;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::infra::tracing::TraceErrorMapping;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(TraceErrorMapping{}.result_code), ResultCode>);

  constexpr std::array<TraceErrorMappingExpectation, 7> kFrozenMappings{ {
      {TraceErrorCode::ProviderNotReady,
       "TRC_E_PROVIDER_NOT_READY",
       ResultCode::ProviderTimeout,
       "6.6 ITracerProvider"},
      {TraceErrorCode::InvalidContext,
       "TRC_E_INVALID_CONTEXT",
       ResultCode::ValidationFieldMissing,
       "6.3 ContextPropagationAdapter"},
      {TraceErrorCode::QueueFull,
       "TRC_E_QUEUE_FULL",
       ResultCode::RuntimeRetryExhausted,
       "6.8 queue exception"},
      {TraceErrorCode::ExportTimeout,
       "TRC_E_EXPORT_TIMEOUT",
       ResultCode::ProviderTimeout,
       "6.6 ISpanExporter"},
      {TraceErrorCode::ExportFailure,
       "TRC_E_EXPORT_FAILURE",
       ResultCode::ProviderTimeout,
       "6.8 export exception"},
      {TraceErrorCode::ShutdownTimeout,
       "TRC_E_SHUTDOWN_TIMEOUT",
       ResultCode::RuntimeRetryExhausted,
       "6.8 shutdown exception"},
      {TraceErrorCode::ConfigInvalid,
       "TRC_E_CONFIG_INVALID",
       ResultCode::ValidationFieldMissing,
       "6.6 ITracerProvider"},
  } };

  for (const auto& expectation : kFrozenMappings) {
    const auto mapping = map_trace_error_code(expectation.code);
    assert_equal(static_cast<int>(expectation.result_code),
                 static_cast<int>(mapping.result_code),
                 std::string("trace error mapping should remain frozen for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.name),
                 std::string(trace_error_code_name(expectation.code)),
                 std::string("trace error code name should remain stable for ") +
                     std::string(expectation.name));
    assert_equal(std::string(expectation.source_anchor),
                 std::string(mapping.source_anchor),
                 std::string("trace error source anchor should remain frozen for ") +
                     std::string(expectation.name));
    assert_true(!mapping.reason.empty(),
                "each tracing private error mapping should carry a non-empty reason");
  }
}

void test_trace_error_names_stay_local_to_tracing_boundary() {
  using dasall::infra::tracing::trace_error_code_name;
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
    assert_true(trace_error_code_name(code).starts_with("TRC_E_"),
                "tracing private error names should remain inside the TRC_E_* local namespace");
  }
}

}  // namespace

int main() {
  try {
    test_trace_error_mapping_matrix_stays_frozen();
    test_trace_error_names_stay_local_to_tracing_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
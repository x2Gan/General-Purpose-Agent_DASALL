#include <exception>
#include <iostream>

#include "ServiceTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::contracts::ErrorDetails;
using dasall::contracts::ErrorInfo;
using dasall::contracts::ErrorSourceRefMinimal;
using dasall::contracts::ResultCode;
using dasall::contracts::classify_result_code;
using dasall::services::DataCatalogResult;
using dasall::services::DataQueryResult;
using dasall::services::ExecutionCommandResult;
using dasall::services::ExecutionDiagnoseResult;
using dasall::services::ExecutionQueryResult;
using dasall::services::ExecutionSubscriptionResult;
using dasall::services::service_result_effective_failure_code;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] ErrorInfo make_error(ResultCode code) {
  return ErrorInfo{
      .failure_type = classify_result_code(code),
      .retryable = false,
      .safe_to_replan = false,
      .details = ErrorDetails{
          .code = static_cast<int>(code),
          .message = "service result semantics contract failure",
          .stage = "tool-bridge",
      },
      .source_ref = ErrorSourceRefMinimal{
          .ref_type = "service_call",
          .ref_id = "svc-result-semantics",
      },
  };
}

void test_effective_failure_code_prefers_explicit_code() {
  const auto effective_code = service_result_effective_failure_code(
      ResultCode::ToolExecutionFailed,
      make_error(ResultCode::ToolExecutionFailed));

  assert_true(effective_code.has_value(),
              "service result semantics should preserve explicit failure codes");
  assert_equal(static_cast<int>(ResultCode::ToolExecutionFailed),
               static_cast<int>(*effective_code),
               "service result semantics should prefer the explicit code when both code and error are present");
}

void test_effective_failure_code_can_be_derived_from_error_details() {
  const auto effective_code = service_result_effective_failure_code(
      std::nullopt,
      make_error(ResultCode::ProviderTimeout));

  assert_true(effective_code.has_value(),
              "service result semantics should derive the failure code from ErrorInfo when code is omitted");
  assert_equal(static_cast<int>(ResultCode::ProviderTimeout),
               static_cast<int>(*effective_code),
               "service result semantics should use ErrorInfo.details.code as the effective failure code");
}

void test_effective_failure_code_rejects_out_of_range_error_codes() {
  auto error = make_error(ResultCode::ProviderTimeout);
  error.details.code = 9999;

  const auto effective_code = service_result_effective_failure_code(std::nullopt, error);

  assert_true(!effective_code.has_value(),
              "service result semantics should reject out-of-range raw error codes");
}

void test_success_results_remain_code_free_across_service_result_objects() {
  const ExecutionCommandResult command_result{
      .code = std::nullopt,
      .execution_id = "exec-service-semantics-success",
      .payload_json = "{\"ok\":true}",
      .side_effects = {},
      .compensation_hints = {},
      .error = std::nullopt,
  };
  const ExecutionQueryResult query_result{
      .code = std::nullopt,
      .state = "completed",
      .snapshot_json = "{}",
      .from_cache = false,
      .error = std::nullopt,
  };
  const ExecutionSubscriptionResult subscription_result{
      .code = std::nullopt,
      .events_json = "[]",
      .next_cursor = std::nullopt,
      .resync_required = false,
      .dropped_count = 0U,
      .error = std::nullopt,
  };
  const ExecutionDiagnoseResult diagnose_result{
      .code = std::nullopt,
      .target_reachable = true,
      .report_json = "{}",
      .error = std::nullopt,
  };
  const DataQueryResult data_query_result{
      .code = std::nullopt,
      .rows_json = "[]",
      .from_cache = false,
      .error = std::nullopt,
  };
  const DataCatalogResult data_catalog_result{
      .code = std::nullopt,
      .catalog_json = "[]",
      .error = std::nullopt,
  };

  assert_true(command_result.has_consistent_values() && command_result.succeeded(),
              "service result semantics should treat code-free execution command results as success");
  assert_true(query_result.has_consistent_values() && query_result.succeeded(),
              "service result semantics should treat code-free execution query results as success");
  assert_true(subscription_result.has_consistent_values() && subscription_result.succeeded(),
              "service result semantics should treat code-free execution subscription results as success");
  assert_true(diagnose_result.has_consistent_values() && diagnose_result.succeeded(),
              "service result semantics should treat code-free diagnose results as success");
  assert_true(data_query_result.has_consistent_values() && data_query_result.succeeded(),
              "service result semantics should treat code-free data query results as success");
  assert_true(data_catalog_result.has_consistent_values() && data_catalog_result.succeeded(),
              "service result semantics should treat code-free data catalog results as success");
}

void test_failure_results_require_code_and_error_to_match() {
  const ExecutionCommandResult consistent_failure{
      .code = ResultCode::ToolExecutionFailed,
      .execution_id = "exec-service-semantics-failure",
      .payload_json = "",
      .side_effects = {},
      .compensation_hints = {"retry-later"},
      .error = make_error(ResultCode::ToolExecutionFailed),
  };
  ExecutionQueryResult missing_error{
      .code = ResultCode::ToolExecutionFailed,
      .state = "failed",
      .snapshot_json = "{}",
      .from_cache = false,
      .error = std::nullopt,
  };
  ExecutionDiagnoseResult mismatched_category{
      .code = ResultCode::ToolExecutionFailed,
      .target_reachable = false,
      .report_json = "{}",
      .error = make_error(ResultCode::RuntimeRetryExhausted),
  };
  DataQueryResult mismatched_detail_code{
      .code = ResultCode::ToolExecutionFailed,
      .rows_json = "[]",
      .from_cache = false,
      .error = make_error(ResultCode::ToolExecutionFailed),
  };
  mismatched_detail_code.error->details.code = static_cast<int>(ResultCode::ProviderTimeout);

  assert_true(consistent_failure.has_consistent_values(),
              "service result semantics should accept matching failure triads");
  assert_true(!consistent_failure.succeeded(),
              "service result semantics should keep matching failure triads off the success path");
  assert_true(!missing_error.has_consistent_values(),
              "service result semantics should reject failure codes without ErrorInfo");
  assert_true(!mismatched_category.has_consistent_values(),
              "service result semantics should reject mismatched failure categories");
  assert_true(!mismatched_detail_code.has_consistent_values(),
              "service result semantics should reject mismatched detail codes");
}

}  // namespace

int main() {
  try {
    test_effective_failure_code_prefers_explicit_code();
    test_effective_failure_code_can_be_derived_from_error_details();
    test_effective_failure_code_rejects_out_of_range_error_codes();
    test_success_results_remain_code_free_across_service_result_objects();
    test_failure_results_require_code_and_error_to_match();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
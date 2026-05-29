#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "logging/LogQueryService.h"
#include "support/TestAssertions.h"

namespace {

class VectorLogQueryRecordReader final
    : public dasall::infra::logging::ILogQueryRecordReader {
 public:
  explicit VectorLogQueryRecordReader(
      std::vector<dasall::infra::LogEvent> records)
      : records_(std::move(records)) {}

  [[nodiscard]] std::vector<dasall::infra::LogEvent> read_window(
      std::int64_t start_ts_ms,
      std::int64_t end_ts_ms) override {
    std::vector<dasall::infra::LogEvent> filtered;
    for (const auto& record : records_) {
      if (!record.has_timestamp()) {
        continue;
      }

      if (*record.ts >= start_ts_ms && *record.ts <= end_ts_ms) {
        filtered.push_back(record);
      }
    }

    return filtered;
  }

 private:
  std::vector<dasall::infra::LogEvent> records_;
};

[[nodiscard]] dasall::infra::LogEvent make_record(std::string trace_id,
                                                  std::string session_id,
                                                  std::string request_id,
                                                  std::string message,
                                                  std::int64_t ts) {
  return dasall::infra::LogEvent{
      .level = dasall::infra::LogLevel::Info,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {
          {"trace_id", std::move(trace_id)},
          {"session_id", std::move(session_id)},
            {"request_id", std::move(request_id)},
      },
      .ts = ts,
  };
}

[[nodiscard]] dasall::infra::policy::PolicyDecisionRef make_policy_decision(
    dasall::infra::policy::PolicyDecision decision) {
  return dasall::infra::policy::PolicyDecisionRef{
      .decision = decision,
      .reason_code = decision == dasall::infra::policy::PolicyDecision::Allow
                         ? std::string("allow_diag_pull")
                         : std::string("deny_diag_pull"),
      .matched_rule_ids = {std::string("diag-query-rule")},
      .snapshot_id = std::string("policy-snapshot-001"),
      .generation = 7,
      .evidence_ref = std::string("policy://diag/query/allow-proof"),
      .warnings = {},
  };
}

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context(
    dasall::infra::policy::PolicyDecision decision =
        dasall::infra::policy::PolicyDecision::Allow) {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = make_policy_decision(decision),
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-log-query-001"),
          .session_id = std::string("session-log-query-001"),
          .trace_id = std::string("trace-log-query-001"),
          .task_id = std::string("task-log-query-001"),
          .parent_task_id = std::string("parent-log-query-001"),
          .lease_id = std::string("lease-log-query-001"),
      },
  };
}

void test_log_query_service_rejects_invalid_request_shape() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::tests::support::assert_true;

  auto reader = std::make_shared<VectorLogQueryRecordReader>(
      std::vector<dasall::infra::LogEvent>{});
  LogQueryService service(reader);

  const auto invalid_result = service.query(LogQueryRequest{
                                                .query_id = std::string("query-invalid"),
                                                .selector_kind = LogQuerySelectorKind::TraceId,
                                                .selector_value = std::string("trace-a"),
                                                .start_ts_ms = 1712140800000,
                                                .end_ts_ms = 1712140801000,
                                                .max_records = 0,
                                            },
                                            make_access_context());

  assert_true(!invalid_result.ok,
              "log query service should reject zero max_records before reading any local records");
  assert_true(invalid_result.result_code == ResultCode::ValidationFieldMissing,
              "invalid log query requests should map to ValidationFieldMissing");
  assert_true(invalid_result.references_only_contract_error_types(),
              "invalid log query requests should stay inside contracts ResultCode/ErrorInfo types");
}

void test_log_query_service_rejects_missing_or_non_allow_access_proof() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LogQueryAccessContext;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::tests::support::assert_true;

  auto reader = std::make_shared<VectorLogQueryRecordReader>(
      std::vector<dasall::infra::LogEvent>{});
  LogQueryService service(reader);
  const LogQueryRequest request{
      .query_id = std::string("query-access"),
      .selector_kind = LogQuerySelectorKind::TraceId,
      .selector_value = std::string("trace-a"),
      .start_ts_ms = 1712140800000,
      .end_ts_ms = 1712140801000,
      .max_records = 1,
  };

  auto missing_proof_context = make_access_context();
  missing_proof_context.policy_decision_ref = {};
  const auto missing_proof_result = service.query(request, missing_proof_context);
  assert_true(!missing_proof_result.ok,
              "log query service should reject access contexts that omit a complete policy decision proof");
  assert_true(missing_proof_result.result_code == ResultCode::ValidationFieldMissing,
              "missing allow proof should map to ValidationFieldMissing");

  const auto denied_result = service.query(
      request,
      make_access_context(dasall::infra::policy::PolicyDecision::Deny));
  assert_true(!denied_result.ok,
              "log query service should reject non-Allow decisions without performing a second authorization step");
  assert_true(denied_result.result_code == ResultCode::PolicyDenied,
              "non-Allow log query requests should map to PolicyDenied");
}

void test_log_query_service_respects_enable_diag_pull_gate() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LogQueryServiceOptions;
  using dasall::tests::support::assert_true;

  auto reader = std::make_shared<VectorLogQueryRecordReader>(
      std::vector<dasall::infra::LogEvent>{});
  LogQueryService service(reader, LogQueryServiceOptions{.enable_diag_pull = false});

  const auto result = service.query(LogQueryRequest{
                                        .query_id = std::string("query-gate"),
                                        .selector_kind = LogQuerySelectorKind::SessionId,
                                        .selector_value = std::string("session-a"),
                                        .start_ts_ms = 1712140800000,
                                        .end_ts_ms = 1712140801000,
                                        .max_records = 1,
                                    },
                                    make_access_context());

  assert_true(!result.ok,
              "log query service should reject local artifact generation when enable_diag_pull is disabled");
  assert_true(result.result_code == ResultCode::PolicyDenied,
              "disabled diag_pull gate should map to PolicyDenied");
}

void test_log_query_service_requires_a_local_record_reader() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::tests::support::assert_true;

  LogQueryService service(nullptr);
  const auto result = service.query(LogQueryRequest{
                                        .query_id = std::string("query-no-reader"),
                                        .selector_kind = LogQuerySelectorKind::TraceId,
                                        .selector_value = std::string("trace-a"),
                                        .start_ts_ms = 1712140800000,
                                        .end_ts_ms = 1712140801000,
                                        .max_records = 1,
                                    },
                                    make_access_context());

  assert_true(!result.ok,
              "log query service should fail fast when no local record reader is available");
  assert_true(result.result_code == ResultCode::ToolExecutionFailed,
              "missing local record reader should map to ToolExecutionFailed");
}

void test_log_query_service_returns_local_artifact_summary_for_exact_trace_query() {
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto reader = std::make_shared<VectorLogQueryRecordReader>(
      std::vector<dasall::infra::LogEvent>{
          make_record("trace-a", "session-a", "request-a", "matching-1", 1712140800001),
          make_record("trace-a", "session-a", "request-b", "matching-2", 1712140800002),
          make_record("trace-b", "session-b", "request-c", "non-match", 1712140800003),
      });

  LogQueryService service(reader,
                          {},
                          []() { return static_cast<std::int64_t>(1712140800100); });
  const auto result = service.query(LogQueryRequest{
                                        .query_id = std::string("query-trace-001"),
                                        .selector_kind = LogQuerySelectorKind::TraceId,
                                        .selector_value = std::string("trace-a"),
                                        .start_ts_ms = 1712140800000,
                                        .end_ts_ms = 1712140800010,
                                        .max_records = 2,
                                    },
                                    make_access_context());

  assert_true(result.ok && result.has_success_payload(),
              "log query service should return a local artifact summary for an exact trace selector hit");
  assert_equal(2,
               static_cast<int>(result.match_count),
               "log query service should count the matching trace records inside the selected window");
  assert_true(!result.truncated,
              "log query service should not truncate when the match count stays within max_records");
  assert_true(result.artifact_ref == "diag://infra/logging/query/query-trace-001",
              "log query service should emit the frozen local artifact_ref namespace");
  assert_true(result.checksum.find("log-query:query-trace-001") == 0,
              "log query service should emit a stable local artifact checksum prefix");
  assert_true(result.created_at == 1712140800100,
              "log query service should preserve the injected created_at timestamp for deterministic artifact summaries");
}

void test_log_query_service_returns_local_artifact_summary_for_exact_request_query() {
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto reader = std::make_shared<VectorLogQueryRecordReader>(
      std::vector<dasall::infra::LogEvent>{
          make_record("trace-a", "session-a", "request-a", "non-match", 1712140800001),
          make_record("trace-b", "session-b", "request-target", "matching", 1712140800002),
      });

  LogQueryService service(reader,
                          {},
                          []() { return static_cast<std::int64_t>(1712140800200); });
  const auto result = service.query(LogQueryRequest{
                                        .query_id = std::string("query-request-001"),
                                        .selector_kind = LogQuerySelectorKind::RequestId,
                                        .selector_value = std::string("request-target"),
                                        .start_ts_ms = 1712140800000,
                                        .end_ts_ms = 1712140800010,
                                        .max_records = 4,
                                    },
                                    make_access_context());

  assert_true(result.ok && result.has_success_payload(),
              "log query service should return a local artifact summary for an exact request selector hit");
  assert_equal(1,
               static_cast<int>(result.match_count),
               "log query service should count only the matching request_id record");
  assert_true(result.artifact_ref == "diag://infra/logging/query/query-request-001",
              "log query service should keep the frozen artifact namespace for request-id queries");
}

}  // namespace

int main() {
  try {
    test_log_query_service_rejects_invalid_request_shape();
    test_log_query_service_rejects_missing_or_non_allow_access_proof();
    test_log_query_service_respects_enable_diag_pull_gate();
    test_log_query_service_requires_a_local_record_reader();
    test_log_query_service_returns_local_artifact_summary_for_exact_trace_query();
    test_log_query_service_returns_local_artifact_summary_for_exact_request_query();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
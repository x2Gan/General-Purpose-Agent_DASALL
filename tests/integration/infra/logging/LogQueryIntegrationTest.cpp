#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "logging/LogQueryService.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

class RecordingDispatchBackend final : public dasall::infra::logging::ILogDispatchBackend {
 public:
  dasall::infra::logging::LogWriteResult dispatch(
      const dasall::infra::LogEvent& event) override {
    events_.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  [[nodiscard]] const std::vector<dasall::infra::LogEvent>& events() const {
    return events_;
  }

 private:
  std::vector<dasall::infra::LogEvent> events_;
};

class RecordingLogQueryRecordReader final
    : public dasall::infra::logging::ILogQueryRecordReader {
 public:
  explicit RecordingLogQueryRecordReader(
      const std::vector<dasall::infra::LogEvent>* events)
      : events_(events) {}

  [[nodiscard]] std::vector<dasall::infra::LogEvent> read_window(
      std::int64_t start_ts_ms,
      std::int64_t end_ts_ms) override {
    std::vector<dasall::infra::LogEvent> filtered;
    if (events_ == nullptr) {
      return filtered;
    }

    for (const auto& event : *events_) {
      if (!event.has_timestamp()) {
        continue;
      }

      if (*event.ts >= start_ts_ms && *event.ts <= end_ts_ms) {
        filtered.push_back(event);
      }
    }

    return filtered;
  }

 private:
  const std::vector<dasall::infra::LogEvent>* events_ = nullptr;
};

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context() {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Allow,
          .reason_code = std::string("allow_diag_pull"),
          .matched_rule_ids = {std::string("diag-query-rule")},
          .snapshot_id = std::string("policy-snapshot-int-001"),
          .generation = 9,
          .evidence_ref = std::string("policy://diag/query/integration"),
          .warnings = {},
      },
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-int-log-query-001"),
          .session_id = std::string("session-int-log-query-001"),
          .trace_id = std::string("trace-int-log-query-001"),
          .task_id = std::string("task-int-log-query-001"),
          .parent_task_id = std::string("parent-int-log-query-001"),
          .lease_id = std::string("lease-int-log-query-001"),
      },
  };
}

void test_log_query_integration_queries_trace_and_session_from_enriched_events() {
  using dasall::infra::InfraContext;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto backend = std::make_unique<RecordingDispatchBackend>();
  auto* backend_ptr = backend.get();
  LoggingFacade facade(std::move(backend));

  const auto init_result = facade.init(InfraContext{
      .request_id = std::string("req-int-log-query-001"),
      .session_id = std::string("session-int-log-query-001"),
      .trace_id = std::string("trace-int-log-query-001"),
      .task_id = std::string("task-int-log-query-001"),
      .parent_task_id = std::string("parent-int-log-query-001"),
      .lease_id = std::string("lease-int-log-query-001"),
  });
  assert_true(init_result.ok,
              "log query integration should initialize the logging facade before capturing queryable records");

  assert_true(facade.log(LogEvent{
                  .level = LogLevel::Info,
                  .module = std::string("runtime"),
                  .message = std::string("trace-session-query-hit-1"),
                  .attrs = {{"event_kind", "query_integration"}},
                  .ts = 1712217600100,
              }).ok,
              "log query integration should capture the first enriched record");
  assert_true(facade.log(LogEvent{
                  .level = LogLevel::Info,
                  .module = std::string("runtime"),
                  .message = std::string("trace-session-query-hit-2"),
                  .attrs = {{"event_kind", "query_integration"}},
                  .ts = 1712217600200,
              }).ok,
              "log query integration should capture the second enriched record");

  auto reader = std::make_shared<RecordingLogQueryRecordReader>(&backend_ptr->events());
  LogQueryService service(reader,
                          {},
                          []() { return static_cast<std::int64_t>(1712217600300); });

  const auto trace_result = service.query(LogQueryRequest{
                                              .query_id = std::string("log-query-trace-001"),
                                              .selector_kind = LogQuerySelectorKind::TraceId,
                                              .selector_value = std::string("trace-int-log-query-001"),
                                              .start_ts_ms = 1712217600000,
                                              .end_ts_ms = 1712217600300,
                                              .max_records = 8,
                                          },
                                          make_access_context());
  assert_true(trace_result.ok && trace_result.has_success_payload(),
              "log query integration should return a local artifact summary for an enriched trace_id selector");
  assert_equal(2,
               static_cast<int>(trace_result.match_count),
               "log query integration should find both enriched records through the trace selector");
  assert_true(trace_result.artifact_ref ==
                  "diag://infra/logging/query/log-query-trace-001",
              "log query integration should keep the frozen local artifact namespace for trace queries");

  const auto session_result = service.query(LogQueryRequest{
                                                .query_id = std::string("log-query-session-001"),
                                                .selector_kind = LogQuerySelectorKind::SessionId,
                                                .selector_value = std::string("session-int-log-query-001"),
                                                .start_ts_ms = 1712217600000,
                                                .end_ts_ms = 1712217600300,
                                                .max_records = 1,
                                            },
                                            make_access_context());
  assert_true(session_result.ok && session_result.has_success_payload(),
              "log query integration should return a local artifact summary for an enriched session_id selector");
  assert_equal(1,
               static_cast<int>(session_result.match_count),
               "log query integration should cap exported record count at max_records for session selectors");
  assert_true(session_result.truncated,
              "log query integration should mark the artifact summary as truncated when more records match than may be exported locally");
  assert_true(session_result.checksum.find("log-query:log-query-session-001") == 0,
              "log query integration should expose a stable checksum prefix for local session artifacts");
  assert_equal(1712217600300,
               static_cast<int>(session_result.created_at),
               "log query integration should preserve the injected artifact creation timestamp");
}

}  // namespace

int main() {
  try {
    test_log_query_integration_queries_trace_and_session_from_enriched_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
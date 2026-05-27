#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "logging/FileLogReader.h"
#include "logging/FileLogSink.h"
#include "logging/LogQueryService.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

class ScopedTempDir {
 public:
  explicit ScopedTempDir(const std::string& stem)
      : path_(fs::temp_directory_path() /
              (stem + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    fs::create_directories(path_);
  }

  ~ScopedTempDir() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context() {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Allow,
          .reason_code = std::string("allow_diag_pull"),
          .matched_rule_ids = {std::string("diag-query-rule")},
          .snapshot_id = std::string("policy-snapshot-int-009"),
          .generation = 13,
          .evidence_ref = std::string("policy://diag/query/integration-009"),
          .warnings = {},
      },
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-int-log-query-009"),
          .session_id = std::string("session-int-log-query-009"),
          .trace_id = std::string("trace-int-log-query-009"),
          .task_id = std::string("task-int-log-query-009"),
          .parent_task_id = std::string("parent-int-log-query-009"),
          .lease_id = std::string("lease-int-log-query-009"),
      },
  };
}

void test_logging_diagnostics_artifact_integration_materializes_redacted_query_artifact_from_file_sink() {
  using dasall::infra::InfraContext;
  using dasall::infra::logging::FileLogPathPolicy;
  using dasall::infra::logging::FileLogReader;
  using dasall::infra::logging::FileLogReaderOptions;
  using dasall::infra::logging::FileLogSink;
  using dasall::infra::logging::FileLogSinkOptions;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LogQueryServiceOptions;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-logging-diagnostics-artifact-int");
  const auto runtime_log_path = temp_dir.path() / "logging" / "runtime.log";
  const auto artifact_root = temp_dir.path() / "query-artifacts";
  fs::create_directories(runtime_log_path.parent_path());

  auto basic_sink = std::make_shared<FileLogSink>(FileLogSinkOptions{
      .file_path = runtime_log_path,
      .state_root_override = {},
      .rotate_max_size_bytes = 4096U,
      .rotate_max_files = 2U,
      .path_policy = FileLogPathPolicy::BuildTreeDefault,
  });
  auto dispatcher = std::make_unique<SinkDispatcher>(SinkDispatcherOptions{
      .queue_options = {},
      .basic_sink = basic_sink,
      .audit_sink = nullptr,
  });

  LoggingFacade facade(std::move(dispatcher));
  assert_true(facade.init(InfraContext{
                  .request_id = std::string("req-int-log-query-009"),
                  .session_id = std::string("session-int-log-query-009"),
                  .trace_id = std::string("trace-int-log-query-009"),
                  .task_id = std::string("task-int-log-query-009"),
                  .parent_task_id = std::string("parent-int-log-query-009"),
                  .lease_id = std::string("lease-int-log-query-009"),
              }).ok,
              "LoggingDiagnosticsArtifactIntegrationTest should initialize the logging facade before writing persisted records");

  assert_true(facade.log(LogEvent{
                  .level = LogLevel::Info,
                  .module = std::string("runtime"),
                  .message = std::string("token=top-secret-value"),
                  .attrs = {{"event_kind", "query_integration"}, {"evidence_ref", "token=top-secret-value"}},
                  .ts = 1712401001000,
              }).ok,
              "LoggingDiagnosticsArtifactIntegrationTest should persist a structured record through FileLogSink");
  assert_true(facade.flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "LoggingDiagnosticsArtifactIntegrationTest should flush the persisted log before querying it");

  auto reader = std::make_shared<FileLogReader>(FileLogReaderOptions{
      .runtime_log_path = runtime_log_path,
      .include_rotation_family = true,
  });
  LogQueryService service(reader,
                          LogQueryServiceOptions{
                              .enable_diag_pull = true,
                              .artifact_namespace = "diag://infra/logging/query",
                              .artifact_root_dir = artifact_root,
                              .index_file_name = "query-index.jsonl",
                              .retention_policy = {.retention_days = 7, .max_artifact_count = 8},
                          },
                          []() { return static_cast<std::int64_t>(1712401001999); });

  const auto result = service.query(LogQueryRequest{
                                        .query_id = std::string("diag-artifact-001"),
                                        .selector_kind = LogQuerySelectorKind::TraceId,
                                        .selector_value = std::string("trace-int-log-query-009"),
                                        .start_ts_ms = 1712401001000,
                                        .end_ts_ms = 1712401001999,
                                        .max_records = 4,
                                    },
                                    make_access_context());

  const auto artifact_path = artifact_root / "diag-artifact-001-1712401001999.json";
  const auto index_path = artifact_root / "query-index.jsonl";
  const auto runtime_log_text = read_text(runtime_log_path);
  const auto artifact_text = read_text(artifact_path);
  const auto index_text = read_text(index_path);

  assert_true(result.ok && result.has_success_payload(),
              "LoggingDiagnosticsArtifactIntegrationTest should materialize a query artifact from the persisted file sink output");
  assert_equal(1,
               static_cast<int>(result.match_count),
               "LoggingDiagnosticsArtifactIntegrationTest should find the structured persisted record by enriched trace_id");
  assert_true(runtime_log_text.find("<redacted>") != std::string::npos &&
                  runtime_log_text.find("top-secret-value") == std::string::npos,
              "LoggingDiagnosticsArtifactIntegrationTest should verify the primary runtime log is already redacted before query materialization");
  assert_true(artifact_text.find("<redacted>") != std::string::npos &&
                  artifact_text.find("top-secret-value") == std::string::npos,
              "LoggingDiagnosticsArtifactIntegrationTest should keep query artifact payload redacted on export");
  assert_true(index_text.find("diag-artifact-001") != std::string::npos &&
                  index_text.find("trace_id") != std::string::npos,
              "LoggingDiagnosticsArtifactIntegrationTest should append owner-safe metadata into the query artifact index");
}

}  // namespace

int main() {
  try {
    test_logging_diagnostics_artifact_integration_materializes_redacted_query_artifact_from_file_sink();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "logging/FileLogReader.h"
#include "logging/LogQueryService.h"
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

[[nodiscard]] dasall::infra::policy::PolicyDecisionRef make_policy_decision() {
  return dasall::infra::policy::PolicyDecisionRef{
      .decision = dasall::infra::policy::PolicyDecision::Allow,
      .reason_code = std::string("allow_diag_pull"),
      .matched_rule_ids = {std::string("diag-query-rule")},
      .snapshot_id = std::string("policy-snapshot-009"),
      .generation = 11,
      .evidence_ref = std::string("policy://diag/query/persisted"),
      .warnings = {},
  };
}

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context() {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = make_policy_decision(),
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-log-query-009"),
          .session_id = std::string("session-log-query-009"),
          .trace_id = std::string("trace-log-query-009"),
          .task_id = std::string("task-log-query-009"),
          .parent_task_id = std::string("parent-log-query-009"),
          .lease_id = std::string("lease-log-query-009"),
      },
  };
}

void write_text(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << text;
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void test_log_query_service_reads_rotated_persisted_logs_and_materializes_artifact() {
  using dasall::infra::logging::FileLogReader;
  using dasall::infra::logging::FileLogReaderOptions;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LogQueryServiceOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-log-query-persisted-reader");
  const auto runtime_log_path = temp_dir.path() / "runtime.log";
  const auto artifact_root = temp_dir.path() / "query-artifacts";

  write_text(temp_dir.path() / "runtime.log.2",
             "{\"schema_version\":\"dasall.logging.event.v1\",\"level\":\"info\",\"module\":\"runtime\",\"message\":\"older token=abc123\",\"ts_ms\":1712300000001,\"attrs\":{\"trace_id\":\"trace-rotated\",\"session_id\":\"session-rotated\",\"event_name\":\"older\"}}\n");
  write_text(temp_dir.path() / "runtime.log.1",
             "{\"schema_version\":\"dasall.logging.event.v1\",\"level\":\"info\",\"module\":\"runtime\",\"message\":\"mid token=abc123\",\"ts_ms\":1712300000002,\"attrs\":{\"trace_id\":\"trace-rotated\",\"session_id\":\"session-rotated\",\"event_name\":\"mid\"}}\n");
  write_text(runtime_log_path,
             "{\"schema_version\":\"dasall.logging.event.v1\",\"level\":\"info\",\"module\":\"runtime\",\"message\":\"newest token=abc123\",\"ts_ms\":1712300000003,\"attrs\":{\"trace_id\":\"trace-rotated\",\"session_id\":\"session-rotated\",\"event_name\":\"newest\"}}\n"
             "{\"schema_version\":\"dasall.logging.event.v1\",\"level\":\"info\",\"module\":\"runtime\",\"message\":\"other trace\",\"ts_ms\":1712300000004,\"attrs\":{\"trace_id\":\"trace-other\",\"session_id\":\"session-other\",\"event_name\":\"skip\"}}\n");

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
                          []() { return static_cast<std::int64_t>(1712300000999); });

  const auto result = service.query(LogQueryRequest{
                                        .query_id = std::string("persisted-trace-001"),
                                        .selector_kind = LogQuerySelectorKind::TraceId,
                                        .selector_value = std::string("trace-rotated"),
                                        .start_ts_ms = 1712300000000,
                                        .end_ts_ms = 1712300000010,
                                        .max_records = 3,
                                    },
                                    make_access_context());

  const auto artifact_path = artifact_root / "persisted-trace-001-1712300000999.json";
  const auto index_path = artifact_root / "query-index.jsonl";
  const auto artifact_text = read_text(artifact_path);
  const auto index_text = read_text(index_path);

  assert_true(result.ok && result.has_success_payload(),
              "LogQueryServicePersistedReaderTest should materialize a diagnostics artifact from rotated persisted logs");
  assert_equal(3,
               static_cast<int>(result.match_count),
               "LogQueryServicePersistedReaderTest should count matches across runtime.log and rotation family");
  assert_true(fs::exists(artifact_path) && fs::exists(index_path),
              "LogQueryServicePersistedReaderTest should write both artifact payload and owner-safe metadata index");
  assert_true(artifact_text.find("older") != std::string::npos &&
                  artifact_text.find("newest") != std::string::npos,
              "LogQueryServicePersistedReaderTest should preserve records from rotated and active files in one artifact payload");
  assert_true(artifact_text.find("token=abc123") == std::string::npos &&
                  artifact_text.find("<redacted>") != std::string::npos,
              "LogQueryServicePersistedReaderTest should re-apply redaction while materializing query artifacts");
  assert_true(index_text.find("persisted-trace-001") != std::string::npos &&
                  index_text.find("trace_id") != std::string::npos,
              "LogQueryServicePersistedReaderTest should append owner-safe index metadata for persisted artifacts");
}

}  // namespace

int main() {
  try {
    test_log_query_service_reads_rotated_persisted_logs_and_materializes_artifact();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
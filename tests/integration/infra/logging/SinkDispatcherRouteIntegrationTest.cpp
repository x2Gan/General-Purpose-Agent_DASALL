#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

#include "logging/FileLogSink.h"
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

[[nodiscard]] dasall::infra::logging::LogEvent make_event(std::string module,
                                                          std::string message) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Info,
      .module = std::move(module),
      .message = std::move(message),
      .attrs = {{"request_id", "req-sink-dispatcher-route-003"}},
      .ts = 1711968607000,
  };
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void test_sink_dispatcher_routes_runtime_and_audit_events_to_distinct_file_sinks() {
  using dasall::infra::logging::FileLogPathPolicy;
  using dasall::infra::logging::FileLogSink;
  using dasall::infra::logging::FileLogSinkOptions;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ScopedTempDir temp_dir("dasall-sink-dispatcher-route");
  const auto runtime_path = temp_dir.path() / "runtime" / "runtime.log";
  const auto audit_path = temp_dir.path() / "audit" / "audit.log";
  fs::create_directories(runtime_path.parent_path());
  fs::create_directories(audit_path.parent_path());

  auto basic_sink = std::make_shared<FileLogSink>(FileLogSinkOptions{
      .file_path = runtime_path,
      .state_root_override = {},
      .rotate_max_size_bytes = 2048U,
      .rotate_max_files = 2U,
      .path_policy = FileLogPathPolicy::BuildTreeDefault,
  });
  auto audit_sink = std::make_shared<FileLogSink>(FileLogSinkOptions{
      .file_path = audit_path,
      .state_root_override = {},
      .rotate_max_size_bytes = 2048U,
      .rotate_max_files = 2U,
      .path_policy = FileLogPathPolicy::BuildTreeDefault,
  });

  SinkDispatcher dispatcher(SinkDispatcherOptions{
      .queue_options = {},
      .basic_sink = basic_sink,
      .audit_sink = audit_sink,
  });

  auto runtime_event = make_event(
      "runtime",
      R"({"schema_version":"dasall.logging.event.v1","route":"basic","message":"runtime ok"})");
  auto audit_event = make_event(
      "audit",
      R"({"schema_version":"dasall.logging.event.v1","route":"audit","message":"audit ok"})");
  audit_event.attrs.insert_or_assign("evidence_ref", "audit-ev-003");

  const auto runtime_result = dispatcher.dispatch(runtime_event);
  const auto audit_result = dispatcher.dispatch(audit_event);
  assert_true(runtime_result.ok && audit_result.ok,
              "SinkDispatcherRouteIntegrationTest should persist both runtime and audit events when route-specific sinks are configured");
  assert_true(dispatcher.last_route() == SinkRoute::Audit,
              "SinkDispatcherRouteIntegrationTest should still report the audit route for the last routed record");
  assert_equal(1,
               static_cast<int>(dispatcher.dispatched_record_count(SinkRoute::BasicFile)),
               "SinkDispatcherRouteIntegrationTest should count one runtime record on the basic route");
  assert_equal(1,
               static_cast<int>(dispatcher.dispatched_record_count(SinkRoute::Audit)),
               "SinkDispatcherRouteIntegrationTest should count one audit record on the audit route");
  assert_true(fs::exists(runtime_path) && fs::exists(audit_path),
              "SinkDispatcherRouteIntegrationTest should materialize both configured route files");

  const auto runtime_text = read_text(runtime_path);
  const auto audit_text = read_text(audit_path);
  assert_true(runtime_text.find("\"route\":\"basic\"") != std::string::npos,
              "SinkDispatcherRouteIntegrationTest should persist the runtime payload into the basic sink file");
  assert_true(audit_text.find("\"route\":\"audit\"") != std::string::npos,
              "SinkDispatcherRouteIntegrationTest should persist the audit payload into the audit sink file");
}

}  // namespace

int main() {
  try {
    test_sink_dispatcher_routes_runtime_and_audit_events_to_distinct_file_sinks();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "error/ResultCode.h"
#include "logging/FileLogSink.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::logging::LogEvent make_event() {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message =
          R"({"schema_version":"dasall.logging.event.v1","message":"token=<redacted>"})",
      .attrs = {{"request_id", "req-sink-failure-003"}},
      .ts = 1711968607000,
  };
}

void test_sink_dispatcher_surfaces_fail_closed_when_the_sink_path_is_unwritable() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::FileLogPathPolicy;
  using dasall::infra::logging::FileLogSink;
  using dasall::infra::logging::FileLogSinkOptions;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto blocked_path =
      std::filesystem::path("/forbidden/dasall/logging/runtime.log");
  auto basic_sink = std::make_shared<FileLogSink>(FileLogSinkOptions{
      .file_path = blocked_path,
      .state_root_override = {},
      .rotate_max_size_bytes = 2048U,
      .rotate_max_files = 2U,
      .path_policy = FileLogPathPolicy::BuildTreeDefault,
  });

  SinkDispatcher dispatcher(SinkDispatcherOptions{
      .queue_options = {},
      .basic_sink = basic_sink,
      .audit_sink = nullptr,
  });

  const auto result = dispatcher.dispatch(make_event());
  assert_true(!result.ok,
              "LoggingSinkFailureInjectionTest should fail closed when the configured file sink path cannot be materialized");
  assert_true(result.result_code == ResultCode::ProviderTimeout,
              "LoggingSinkFailureInjectionTest should map sink IO failures to the standard provider-timeout result code");
  assert_equal(0,
               static_cast<int>(dispatcher.dispatched_record_count()),
               "LoggingSinkFailureInjectionTest should not mark the failed write as a successfully dispatched record");
  assert_true(!dispatcher.has_last_record(),
              "LoggingSinkFailureInjectionTest should not publish a last-record success snapshot when the sink write fails");
}

}  // namespace

int main() {
  try {
    test_sink_dispatcher_surfaces_fail_closed_when_the_sink_path_is_unwritable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
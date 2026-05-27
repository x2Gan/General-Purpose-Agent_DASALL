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

    const auto dispatch_result = dispatcher.dispatch(make_event());
    assert_true(dispatch_result.ok,
          "LoggingSinkFailureInjectionTest should accept the record into the worker-backed queue before flush observes the sink failure");

    const auto flush_result = dispatcher.flush(
      dasall::infra::logging::LogFlushDeadline{.timeout_ms = 500});
    assert_true(!flush_result.ok,
          "LoggingSinkFailureInjectionTest should fail closed on flush when the configured file sink path cannot be materialized");
    assert_true(flush_result.result_code == ResultCode::ProviderTimeout,
          "LoggingSinkFailureInjectionTest should map sink IO failures to the standard provider-timeout result code");
    assert_equal(1,
           static_cast<int>(dispatcher.dispatched_record_count()),
           "LoggingSinkFailureInjectionTest should keep the accepted queue entry count even when the worker later reports sink failure");
    assert_true(dispatcher.has_last_record(),
          "LoggingSinkFailureInjectionTest should still expose the accepted routed record snapshot before the async sink write fails");
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
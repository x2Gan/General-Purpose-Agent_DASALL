#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "logging/FileLogSink.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

class CapturingRecoverySink final : public dasall::infra::logging::ILogRecoverySink {
 public:
  dasall::infra::logging::LogWriteResult write(
      const dasall::infra::logging::LogEvent& event) override {
    written_events_.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  [[nodiscard]] const std::vector<dasall::infra::logging::LogEvent>& written_events() const {
    return written_events_;
  }

 private:
  std::vector<dasall::infra::logging::LogEvent> written_events_;
};

[[nodiscard]] dasall::infra::logging::LogEvent make_event() {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("Authorization: Bearer recovery-integration-token"),
      .attrs = {{"request_id", "req-recovery-int-001"}},
      .ts = 1712300402001,
  };
}

void test_logging_facade_recovers_to_fallback_when_queue_flush_surfaces_sink_failure() {
  using dasall::infra::logging::FileLogPathPolicy;
  using dasall::infra::logging::FileLogSink;
  using dasall::infra::logging::FileLogSinkOptions;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LoggingErrorCode;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkDispatcherOptions;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const auto blocked_path =
      std::filesystem::path("/forbidden/dasall/logging/runtime.log");
  auto primary_sink = std::make_shared<FileLogSink>(FileLogSinkOptions{
      .file_path = blocked_path,
      .state_root_override = {},
      .rotate_max_size_bytes = 2048U,
      .rotate_max_files = 2U,
      .path_policy = FileLogPathPolicy::BuildTreeDefault,
  });
  auto fallback_sink = std::make_shared<CapturingRecoverySink>();
  auto dispatcher = std::make_unique<SinkDispatcher>(SinkDispatcherOptions{
      .queue_options = {},
      .basic_sink = primary_sink,
      .audit_sink = nullptr,
  });
  LoggingFacade facade(std::move(dispatcher), fallback_sink);

  assert_true(facade.init().ok,
              "LoggingRecoveryIntegrationTest should initialize the facade before exercising queue-flush recovery");

  const auto log_result = facade.log(make_event());
  assert_true(log_result.ok,
              "LoggingRecoveryIntegrationTest should accept the record into the deterministic queue before the worker surfaces the sink failure");

  const auto flush_result = facade.flush(LogFlushDeadline{.timeout_ms = 500});
  assert_true(flush_result.ok,
              "LoggingRecoveryIntegrationTest should downgrade queue-flush sink failures into fallback success when the degraded sink persists the record");
  assert_true(facade.is_degraded() && facade.fallback_active(),
              "LoggingRecoveryIntegrationTest should keep degraded fallback state visible after queue-flush recovery");
  assert_true(facade.last_recovery_error_code() == LoggingErrorCode::SinkIo,
              "LoggingRecoveryIntegrationTest should retain LOG_E_SINK_IO after queue-flush recovery");
  assert_equal(1,
               static_cast<int>(fallback_sink->written_events().size()),
               "LoggingRecoveryIntegrationTest should replay the failed queued record into the fallback sink");
  assert_true(fallback_sink->written_events().front().message.find("recovery-integration-token") ==
                  std::string::npos,
              "LoggingRecoveryIntegrationTest should preserve the redacted structured payload on the fallback path");
}

}  // namespace

int main() {
  try {
    test_logging_facade_recovers_to_fallback_when_queue_flush_surfaces_sink_failure();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
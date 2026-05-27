#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

#include "ObservabilityLiveComposition.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

class TempLogRoot {
 public:
  explicit TempLogRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempLogRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] dasall::infra::config::TypedConfig make_entry(
    std::string key_path,
    dasall::infra::config::ConfigValueType value_type,
    std::string serialized_value,
    dasall::infra::config::ConfigSourceKind source_kind,
    std::string source_id) {
  return dasall::infra::config::TypedConfig{
      .key_path = std::move(key_path),
      .value_type = value_type,
      .serialized_value = std::move(serialized_value),
      .schema_version = std::string(dasall::infra::config::kConfigSchemaVersionV1),
      .source_kind = source_kind,
      .source_id = std::move(source_id),
      .secret_backed = false,
  };
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void test_logging_live_composition_projects_config_into_async_logger() {
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::LoggingFormat;
  using dasall::infra::logging::LoggingOverflowPolicy;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const TempLogRoot log_root("dasall-logging-live-config");
  const auto log_path = log_root.path() / "runtime.log";
  const std::string profile_source = "profiles/desktop_full/runtime_policy.yaml";
  const std::string deploy_source = "deploy://site/logging.yaml";

    ObservabilityLiveCompositionOptions options;
    options.profile_id = "desktop_full";
    options.logging_config_entries = {
      make_entry("infra.logging.level",
           ConfigValueType::String,
           "debug",
           ConfigSourceKind::Profile,
           profile_source),
      make_entry("infra.logging.format",
           ConfigValueType::String,
           "key_value",
           ConfigSourceKind::Profile,
           profile_source),
      make_entry("infra.logging.async.enabled",
           ConfigValueType::Boolean,
           "true",
           ConfigSourceKind::Profile,
           profile_source),
      make_entry("infra.logging.async.queue_size",
           ConfigValueType::UnsignedInteger,
           "2",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.async.overflow_policy",
           ConfigValueType::String,
           "overrun_oldest",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.file.path",
           ConfigValueType::String,
           log_path.string(),
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.file.rotate.max_size_mb",
           ConfigValueType::UnsignedInteger,
           "1",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.file.rotate.max_files",
           ConfigValueType::UnsignedInteger,
           "3",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.redaction.enabled",
           ConfigValueType::Boolean,
           "false",
           ConfigSourceKind::Profile,
           profile_source),
      make_entry("infra.logging.redaction.ruleset",
           ConfigValueType::String,
           "incident_v2",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.export.enable_diag_pull",
           ConfigValueType::Boolean,
           "false",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.audit.required",
           ConfigValueType::Boolean,
           "true",
           ConfigSourceKind::Profile,
           profile_source),
    };

    const auto result = compose_live_observability(options);

  assert_true(result.ok(),
              "live observability composition should accept a frozen logging config projection: " +
                  result.error);
  assert_true(result.active_logging_config.has_value(),
              "live observability composition should retain the applied logging config for validation");
  assert_equal(static_cast<int>(LogLevel::Debug),
               static_cast<int>(result.active_logging_config->level),
               "live observability composition should project the configured log level");
  assert_equal(static_cast<int>(LoggingOverflowPolicy::OverrunOldest),
               static_cast<int>(result.active_logging_config->overflow_policy),
               "live observability composition should retain the configured overflow policy");
  assert_equal(2,
               static_cast<int>(result.active_logging_config->queue_size),
               "live observability composition should retain the configured queue size");
  assert_true(!result.active_logging_config->enable_diag_pull,
              "live observability composition should retain the configured diag pull gate");

  auto* facade = dynamic_cast<LoggingFacade*>(result.logger.get());
  assert_true(facade != nullptr,
              "live observability composition should expose the concrete logging facade for focused validation");
  assert_equal(static_cast<int>(LogLevel::Debug),
               static_cast<int>(facade->current_level()),
               "logging facade should project the configured minimum log level");
  assert_equal(static_cast<int>(LoggingFormat::KeyValue),
               static_cast<int>(facade->current_format()),
               "logging facade should project the configured formatter mode");
  assert_true(!facade->redaction_enabled(),
              "logging facade should project the configured redaction enable flag");
  assert_equal(std::string("incident_v2"),
               facade->redaction_ruleset(),
               "logging facade should retain the configured redaction ruleset");

  const auto* backend = dynamic_cast<const SinkDispatcher*>(facade->dispatch_backend());
  assert_true(backend != nullptr,
              "async-enabled live composition should retain the deterministic queue-backed sink dispatcher");

  const auto write_result = facade->log(LogEvent{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("Authorization: Bearer live-token"),
      .attrs = {{"request_id", "req-live-config-001"},
                {"event_name", "logging.live.config"}},
      .ts = 1712300300001,
  });
  assert_true(write_result.ok,
              "projected live logger should accept writes through the queue-backed dispatcher");
  assert_true(facade->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "projected live logger should flush successfully through the deterministic queue contract");
  assert_true(std::filesystem::exists(log_path),
              "projected live logger should materialize the configured log sink path");

  const auto payload = read_text_file(log_path);
  assert_true(payload.find("schema_version=\"dasall.logging.event.v1\"") != std::string::npos,
              "key_value formatter projection should render key-value payloads into the live sink");
  assert_true(payload.find("live-token") != std::string::npos,
              "disabled redaction should preserve sensitive payloads in the focused projection test");
}

void test_logging_live_composition_projects_rotation_policy_into_file_sink() {
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_true;

  const TempLogRoot log_root("dasall-logging-live-rotation");
  const auto log_path = log_root.path() / "runtime.log";
  const std::string profile_source = "profiles/desktop_full/runtime_policy.yaml";
  const std::string deploy_source = "deploy://site/logging.yaml";
  const std::string large_payload(600U * 1024U, 'x');

    ObservabilityLiveCompositionOptions options;
    options.profile_id = "desktop_full";
    options.logging_config_entries = {
      make_entry("infra.logging.file.path",
           ConfigValueType::String,
           log_path.string(),
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.file.rotate.max_size_mb",
           ConfigValueType::UnsignedInteger,
           "1",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.logging.file.rotate.max_files",
           ConfigValueType::UnsignedInteger,
           "2",
           ConfigSourceKind::DeploymentOverride,
           deploy_source),
      make_entry("infra.audit.required",
           ConfigValueType::Boolean,
           "true",
           ConfigSourceKind::Profile,
           profile_source),
    };

    const auto result = compose_live_observability(options);

  assert_true(result.ok(),
              "live observability composition should accept a focused rotation config projection: " +
                  result.error);

  auto* facade = dynamic_cast<dasall::infra::logging::LoggingFacade*>(result.logger.get());
  assert_true(facade != nullptr,
              "rotation projection test should keep a concrete logging facade");

  for (int index = 0; index < 2; ++index) {
    const auto write_result = facade->log(LogEvent{
        .level = LogLevel::Info,
        .module = std::string("runtime"),
        .message = large_payload,
        .attrs = {{"request_id", "req-rotation-001"},
                  {"sequence", std::to_string(index)}},
        .ts = 1712300301000 + index,
    });
    assert_true(write_result.ok,
                "rotation projection test should accept large payload writes");
    assert_true(facade->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
                "rotation projection test should drain the queue before verifying rotated artifacts");
  }

  assert_true(std::filesystem::exists(log_path),
              "rotation projection should keep the active runtime log file present");
  assert_true(std::filesystem::exists(std::filesystem::path(log_path.string() + ".1")),
              "rotation projection should materialize the configured rotation family in the live sink");
}

}  // namespace

int main() {
  try {
    test_logging_live_composition_projects_config_into_async_logger();
    test_logging_live_composition_projects_rotation_policy_into_file_sink();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
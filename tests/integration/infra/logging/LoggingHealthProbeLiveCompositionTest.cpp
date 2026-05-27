#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "ObservabilityLiveComposition.h"
#include "health/HealthMonitorFacade.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

class TempLogRoot {
 public:
  explicit TempLogRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                                             .count()))) {
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

[[nodiscard]] dasall::infra::logging::LogEvent make_event(
    std::string message,
    std::int64_t timestamp_ms) {
  return dasall::infra::logging::LogEvent{
      .level = dasall::infra::logging::LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::move(message),
      .attrs = {{"event_name", "logging.health.live"}},
      .ts = timestamp_ms,
  };
}

[[nodiscard]] bool snapshot_contains_component(const dasall::infra::HealthSnapshot& snapshot,
                                               const std::string& component) {
  return std::find(snapshot.failed_components.begin(),
                   snapshot.failed_components.end(),
                   component) != snapshot.failed_components.end();
}

void test_logging_health_probe_live_composition_registers_ready_probe() {
  using dasall::infra::HealthMonitorFacade;
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TempLogRoot log_root("dasall-logging-health-live-ready");

  ObservabilityLiveCompositionOptions options;
  options.profile_id = "desktop_full";
  options.logging_state_root_override = log_root.path();

  const auto result = compose_live_observability(options);
  assert_true(result.ok(),
              "logging health live composition should succeed on the installed-authoritative temp log root: " +
                  result.error);

  auto* facade = dynamic_cast<LoggingFacade*>(result.logger.get());
  auto* health_monitor = dynamic_cast<HealthMonitorFacade*>(result.health_monitor.get());
  assert_true(facade != nullptr && health_monitor != nullptr,
              "logging health live composition test should keep concrete logger and health monitor types inspectable");
  assert_equal(1,
               static_cast<int>(health_monitor->registered_probe_count()),
               "compose_live_observability should register exactly one logging readiness probe into HealthMonitorFacade");

  assert_true(facade->log(make_event("live ready path", 1712401001000LL)).ok &&
                  facade->flush(LogFlushDeadline{.timeout_ms = 250}).ok,
              "healthy live composition should accept a log write and flush before probing readiness");

  const auto snapshot = health_monitor->evaluate_now();
  assert_true(snapshot.ok && snapshot.snapshot.is_ready(),
              "registered logging readiness probe should evaluate to a healthy snapshot on the nominal live path");
}

void test_logging_health_probe_live_composition_degrades_when_sink_fallback_activates() {
  using dasall::infra::HealthMonitorFacade;
  using dasall::infra::ObservabilityLiveCompositionOptions;
  using dasall::infra::compose_live_observability;
  using dasall::infra::config::ConfigSourceKind;
  using dasall::infra::config::ConfigValueType;
  using dasall::infra::logging::LoggingFacade;
  using dasall::tests::support::assert_true;

  TempLogRoot log_root("dasall-logging-health-live-degraded");
  const auto missing_parent_log_path =
      log_root.path() / "missing-parent" / "runtime.log";
  const std::string profile_source = "profiles/desktop_full/runtime_policy.yaml";
  const std::string deploy_source = "deploy://site/logging.yaml";

  ObservabilityLiveCompositionOptions options;
  options.profile_id = "desktop_full";
  options.logging_config_entries = {
      make_entry("infra.logging.async.enabled",
                 ConfigValueType::Boolean,
                 "false",
                 ConfigSourceKind::Profile,
                 profile_source),
      make_entry("infra.logging.file.path",
                 ConfigValueType::String,
                 missing_parent_log_path.string(),
                 ConfigSourceKind::DeploymentOverride,
                 deploy_source),
  };

  const auto result = compose_live_observability(options);
  assert_true(result.ok(),
              "logging health live composition should still compose before the first sink write failure: " +
                  result.error);

  auto* facade = dynamic_cast<LoggingFacade*>(result.logger.get());
  auto* health_monitor = dynamic_cast<HealthMonitorFacade*>(result.health_monitor.get());
  assert_true(facade != nullptr && health_monitor != nullptr,
              "degraded live composition test should expose concrete logger and health monitor types");
  assert_true(facade->log(make_event("live degraded path", 1712401002000LL)).ok &&
                  facade->fallback_active(),
              "missing-parent explicit log path should drive the logging facade into degraded fallback mode while preserving an accepted write result");

  const auto snapshot = health_monitor->evaluate_now();
  assert_true(snapshot.ok && snapshot.snapshot.is_degraded_state(),
              "logging health probe should degrade the aggregate health snapshot once sink fallback is active");
  assert_true(snapshot_contains_component(snapshot.snapshot,
                                          std::string("infra.logging.pipeline")),
              "degraded logging health snapshot should point at the frozen infra.logging.pipeline probe name");
}

}  // namespace

int main() {
  try {
    test_logging_health_probe_live_composition_registers_ready_probe();
    test_logging_health_probe_live_composition_degrades_when_sink_fallback_activates();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
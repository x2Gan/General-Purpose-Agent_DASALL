#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "budget/BudgetDecision.h"
#include "checkpoint/RecoveryOutcome.h"
#include "logging/LoggingFacade.h"
#include "safety/SafeModeController.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeEventBus.h"
#include "telemetry/RuntimeTelemetryBridge.h"

namespace {

namespace fs = std::filesystem;

using dasall::contracts::RecoveryOutcome;
using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LoggingFacade;
using dasall::profiles::ProfileCatalog;
using dasall::profiles::RuntimePolicyLoadRequest;
using dasall::profiles::RuntimePolicyProvider;
using dasall::profiles::RuntimePolicySnapshot;
using dasall::runtime::BudgetViolationClass;
using dasall::runtime::RuntimeState;
using dasall::runtime::RuntimeTelemetryContext;
using dasall::runtime::SafeModeAction;
using dasall::runtime::SafeModeDecision;
using dasall::runtime::SafeModeState;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr char kCompositionOwner[] = "gateway.http-unary";

struct ScopedTempDir {
  fs::path path;

  ~ScopedTempDir() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::shared_ptr<const RuntimePolicySnapshot> load_snapshot(
    const std::string& profile_id) {
  const ProfileCatalog catalog(repository_root() / "profiles");
  const RuntimePolicyProvider provider(catalog);
  const auto runtime_result = provider.load_snapshot(RuntimePolicyLoadRequest{
      .profile_id = profile_id,
  });
  assert_true(runtime_result.ok(),
              "runtime production logging integration should load snapshot for " + profile_id);
  assert_true(runtime_result.snapshot != nullptr &&
                  runtime_result.snapshot->has_consistent_values(),
              "runtime production logging integration should keep snapshot values consistent for " +
                  profile_id);
  return runtime_result.snapshot;
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] RuntimeTelemetryContext make_context() {
  return RuntimeTelemetryContext{
      .request_id = std::string("req-runtime-production-logging"),
      .session_id = std::string("session-runtime-production-logging"),
      .trace_id = std::string("trace-runtime-production-logging"),
      .turn_id = std::string("turn-runtime-production-logging"),
      .checkpoint_id = std::string("chk-runtime-production-logging"),
  };
}

void test_runtime_live_composition_persists_redacted_control_plane_events() {
  ScopedTempDir temp_dir{
      .path = fs::temp_directory_path() / "dasall-runtime-production-logging"};
  fs::create_directories(temp_dir.path);
  const auto state_root = temp_dir.path / "state";
  const auto runtime_log_path = state_root / "logging" / "runtime.log";

  const auto snapshot = load_snapshot("desktop_full");
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = repository_root(),
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = {},
          .create_vector_recall_store_override = {},
          .create_query_encoder_override = {},
          .knowledge_query_encoder_transport_override = nullptr,
          .knowledge_refresh_timer = nullptr,
          .knowledge_refresh_source_provider = nullptr,
      });
  assert_true(composition.ok(),
              "runtime production logging integration should compose minimal live dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set != nullptr &&
                  composition.dependency_set->runtime_event_bus != nullptr &&
                  composition.dependency_set->runtime_telemetry_bridge != nullptr &&
                  composition.dependency_set->logger != nullptr,
              "runtime production logging integration should expose logger, event bus, and telemetry bridge from live composition");

  const auto logger =
      std::dynamic_pointer_cast<LoggingFacade>(composition.dependency_set->logger);
  assert_true(logger != nullptr,
              "runtime production logging integration should keep the concrete logger inspectable");

  const auto context = make_context();
  const auto transition_record = composition.dependency_set->runtime_telemetry_bridge->emit_transition(
      RuntimeState::Planning,
      RuntimeState::Reasoning,
      context,
      "transition_secret=hidden payload_json=raw");
  const auto budget_record = composition.dependency_set->runtime_telemetry_bridge->emit_budget_reject(
      dasall::runtime::make_budget_rejected_decision(
          BudgetViolationClass::LatencyExhausted,
          "budget_secret=hidden"),
      context,
      "budget_detail_secret=raw");
  const auto recovery_record = composition.dependency_set->runtime_telemetry_bridge->emit_recovery_reject(
      RecoveryOutcome{
          .executed_action = std::string("abort_safe"),
          .final_runtime_state = std::string("FailedSafe"),
          .updated_retry_count = 2U,
          .checkpoint_ref = std::string("checkpoint-secret"),
          .compensation_result_ref = std::nullopt,
          .rejection_reason = std::string("retry budget exhausted"),
          .escalation_reason = std::nullopt,
      },
      context,
      "recovery_detail_secret=raw");
  const auto safe_mode_record = composition.dependency_set->runtime_telemetry_bridge->emit_safe_mode(
      SafeModeDecision{
          .transition_required = true,
          .previous_mode = SafeModeState::Normal,
          .target_mode = SafeModeState::SafeMode,
          .action = SafeModeAction::EnterSafeMode,
          .target_runtime_state = RuntimeState::SafeMode,
          .error_code = dasall::runtime::RuntimeErrorCode::RT_E_510_SAFE_MODE_ENTERED,
          .selected_fallback = std::string("local-fallback"),
          .detail = "safe_mode_secret=hidden",
      },
      context,
      "safe_mode_detail_secret=raw");
  assert_true(transition_record.envelope.event_name == "runtime.transition" &&
                  budget_record.envelope.event_name == "runtime.budget.reject" &&
                  recovery_record.envelope.event_name == "runtime.recovery.reject" &&
                  safe_mode_record.envelope.event_name == "runtime.safe_mode",
              "runtime production logging integration should emit the expected runtime control-plane event names before persistence");

  assert_equal(4,
               static_cast<int>(composition.dependency_set->runtime_event_bus->dispatch_pending()),
               "runtime production logging integration should dispatch the four control-plane telemetry events through the runtime event bus");
  assert_true(logger->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "runtime production logging integration should flush the logger before inspecting runtime.log");
  assert_true(logger->has_last_dispatched_event(),
              "runtime production logging integration should keep the last runtime event inspectable");
  assert_true(logger->last_dispatched_event().attrs.at("event_name") == "runtime.safe_mode",
              "runtime production logging integration should keep runtime.safe_mode as the last dispatched control-plane log event");

  const auto runtime_log_text = read_text(runtime_log_path);
  assert_true(runtime_log_text.find("runtime.transition") != std::string::npos &&
                  runtime_log_text.find("runtime.budget.reject") != std::string::npos &&
                  runtime_log_text.find("runtime.recovery.reject") != std::string::npos &&
                  runtime_log_text.find("runtime.safe_mode") != std::string::npos,
              "runtime production logging integration should persist transition, budget, recovery, and safe-mode control-plane events into runtime.log");
  assert_true(runtime_log_text.find("\"module\":\"runtime\"") != std::string::npos &&
                  runtime_log_text.find("\"request_id\":\"req-runtime-production-logging\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"session_id\":\"session-runtime-production-logging\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"trace_id\":\"trace-runtime-production-logging\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"runtime_instance_id\":\"gateway.http-unary\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"audit_ref_pending\":\"true\"") !=
                      std::string::npos,
              "runtime production logging integration should persist structured runtime correlation and audit-pending attrs into runtime.log");
  assert_true(runtime_log_text.find("transition_secret") == std::string::npos &&
                  runtime_log_text.find("budget_secret") == std::string::npos &&
                  runtime_log_text.find("budget_detail_secret") == std::string::npos &&
                  runtime_log_text.find("recovery_detail_secret") == std::string::npos &&
                  runtime_log_text.find("safe_mode_secret") == std::string::npos &&
                  runtime_log_text.find("checkpoint-secret") == std::string::npos,
              "runtime production logging integration should not leak control-plane detail or checkpoint payloads into runtime.log");
}

}  // namespace

int main() {
  try {
    test_runtime_live_composition_persists_redacted_control_plane_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

    return 0;
}
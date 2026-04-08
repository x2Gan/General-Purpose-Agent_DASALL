#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "watchdog/TimeoutDecision.h"
#include "watchdog/WatchdogAuditBridge.h"
#include "watchdog/WatchdogErrors.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);

    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.pop_front();
      return outcome;
    }

    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

[[nodiscard]] dasall::infra::watchdog::TimeoutDecision make_decision(
    dasall::infra::watchdog::WatchdogTimeoutLevel timeout_level,
    std::uint32_t consecutive_miss,
    std::string evidence_ref = "watchdog://timeout/runtime.main_loop") {
  return dasall::infra::watchdog::TimeoutDecision{
      .entity_id = std::string("runtime.main_loop"),
      .timeout_level = timeout_level,
      .consecutive_miss = consecutive_miss,
      .reason_code = dasall::contracts::ResultCode::ProviderTimeout,
      .evidence_ref = std::move(evidence_ref),
  };
}

void test_watchdog_audit_bridge_emits_critical_timeout_with_frozen_payload() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::watchdog::WatchdogAuditBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  WatchdogAuditBridge bridge(logger);

  const auto result = bridge.write_timeout_audit(
      make_decision(WatchdogTimeoutLevel::Critical, 3));
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.is_valid(),
              "WatchdogAuditBridge should emit a valid audit payload for critical timeout decisions");
  assert_true(status.is_valid() && status.emitted_total == 1 &&
                  status.skipped_total == 0 && !status.degraded,
              "WatchdogAuditBridge should remain healthy after persisting a critical timeout audit event");
  assert_equal(1, static_cast<int>(logger->events.size()),
               "WatchdogAuditBridge should dispatch one AuditEvent for a critical timeout decision");

  const auto& event = logger->events.front();
  const auto& context = logger->contexts.front();
  assert_equal(std::string("watchdog.timeout_detected"),
               event.action,
               "WatchdogAuditBridge should use the frozen watchdog.timeout_detected action");
  assert_equal(std::string("watchdog_entity:runtime.main_loop"),
               event.target,
               "WatchdogAuditBridge should namespace the supervised entity inside the audit target");
  assert_true(event.outcome == AuditOutcome::Failed,
              "WatchdogAuditBridge should map critical timeout decisions to AuditOutcome::Failed");
  assert_true(event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
                  event.evidence_ref.ref == "watchdog://timeout/runtime.main_loop",
              "WatchdogAuditBridge should keep timeout evidence inside the frozen audit ToolResult reference boundary");
  assert_true(has_side_effect(event, "timeout_level:critical") &&
                  has_side_effect(event, "consecutive_miss:3") &&
                  has_side_effect(event, "reason_code:ProviderTimeout"),
              "WatchdogAuditBridge should serialize timeout level, consecutive miss and reason code as stable side effects");
  assert_equal(std::string("infra.watchdog"),
               context.worker_type,
               "WatchdogAuditBridge should use the frozen infra.watchdog worker_type context");
}

void test_watchdog_audit_bridge_escalates_fatal_timeout_to_audit_outcome_escalated() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::watchdog::WatchdogAuditBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  WatchdogAuditBridge bridge(logger);

  const auto result = bridge.write_timeout_audit(
      make_decision(WatchdogTimeoutLevel::Fatal, 4));

  assert_true(result.emitted && result.is_valid(),
              "WatchdogAuditBridge should emit fatal timeout decisions instead of silently dropping them");
  assert_true(logger->events.front().outcome == AuditOutcome::Escalated,
              "WatchdogAuditBridge should map fatal timeout decisions to AuditOutcome::Escalated");
}

void test_watchdog_audit_bridge_skips_warning_without_degrading_status() {
  using dasall::infra::watchdog::WatchdogAuditBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  WatchdogAuditBridge bridge(logger);

  const auto result = bridge.write_timeout_audit(
      make_decision(WatchdogTimeoutLevel::Warning, 1));
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid(),
              "WatchdogAuditBridge should leave warning-only timeout decisions to metrics and skip audit emission");
  assert_true(logger->events.empty(),
              "WatchdogAuditBridge should not write an AuditEvent for warning-only timeout decisions");
  assert_true(status.is_valid() && status.skipped_total == 1 &&
                  status.emit_failures == 0 && !status.degraded,
              "WatchdogAuditBridge should track warning skips without marking the bridge degraded");
}

void test_watchdog_audit_bridge_surfaces_missing_logger_as_audit_write_failure() {
  using dasall::infra::watchdog::WatchdogAuditBridge;
  using dasall::infra::watchdog::WatchdogErrorCode;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  WatchdogAuditBridge bridge(nullptr);

  const auto result = bridge.write_timeout_audit(
      make_decision(WatchdogTimeoutLevel::Critical, 3));
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid(),
              "WatchdogAuditBridge should surface missing audit sink as an observable failure for critical timeout decisions");
  assert_true(result.references_only_contract_error_types(),
              "WatchdogAuditBridge missing-logger failures should stay inside contracts ResultCode/ErrorInfo categories");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.message.find(
                      std::string(dasall::infra::watchdog::watchdog_error_code_name(
                          WatchdogErrorCode::AuditWriteFail))) != std::string::npos,
              "WatchdogAuditBridge should include the frozen audit write failure token in missing-logger errors");
  assert_true(status.is_valid() && status.emit_failures == 1 && status.degraded,
              "WatchdogAuditBridge should mark status degraded once a required audit sink is missing");
}

void test_watchdog_audit_bridge_surfaces_sink_write_failure_observably() {
  using dasall::infra::watchdog::WatchdogAuditBridge;
  using dasall::infra::watchdog::WatchdogTimeoutLevel;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = dasall::contracts::ResultCode::ToolExecutionFailed,
  });
  WatchdogAuditBridge bridge(logger);

  const auto result = bridge.write_timeout_audit(
      make_decision(WatchdogTimeoutLevel::Critical, 3));
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid(),
              "WatchdogAuditBridge should surface sink write failures instead of treating them as successful audit writes");
  assert_true(result.references_only_contract_error_types(),
              "WatchdogAuditBridge sink write failures should remain within contracts error typing");
  assert_true(status.is_valid() && status.emit_failures == 1 && status.degraded,
              "WatchdogAuditBridge should count sink write failures in bridge status");
}

}  // namespace

int main() {
  try {
    test_watchdog_audit_bridge_emits_critical_timeout_with_frozen_payload();
    test_watchdog_audit_bridge_escalates_fatal_timeout_to_audit_outcome_escalated();
    test_watchdog_audit_bridge_skips_warning_without_degrading_status();
    test_watchdog_audit_bridge_surfaces_missing_logger_as_audit_write_failure();
    test_watchdog_audit_bridge_surfaces_sink_write_failure_observably();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}